/* Wired USB HID gamepad support: Xbox One, PS4 (DualShock 4), PS3
   (Sixaxis/DualShock 3). Single active pad at a time — first recognized
   controller found on USB wins; matches this port's existing single-active-
   controller philosophy (see wii_input.c's s_active_ctrl_type).

   Uses libogc's raw USB stack (ogc/usb.h) directly — no extra library, part
   of core -logc. Enumeration/open happen at init and on hotplug notify;
   reads are async (USB_ReadIntrMsgAsync) with immediate rearm on completion
   so a stalled/unplugged pad can never block Com_Frame.

   Protocol notes (byte offsets, init sequences) are drawn from the public,
   well-documented Linux drivers this exact functionality is modeled on:
   drivers/input/joystick/xpad.c (Xbox One) and drivers/hid/hid-sony.c (PS3).
   The Xbox One power-on packet and report layout below were verified
   against the current upstream xpad.c source. The PS3 report byte offsets
   are the commonly-cited community-reverse-engineered layout (the upstream
   driver parses Sixaxis input through the generic HID report-descriptor
   path rather than fixed offsets) — marked VERIFY ON HARDWARE below; if a
   DS3 pad connects but buttons/axes read wrong, re-check against a USB
   capture or hid-sony.c's report descriptor handling before trusting these
   offsets further. PS4 offsets are the widely-used community-standard USB
   HID layout (SDL2/hidapi-adjacent projects agree on this layout). */

#include <gccore.h>
#include <ogc/usb.h>
#include <string.h>
#include <malloc.h>

#include "wii_usb_hid.h"
#include "qcommon/q_shared.h"

typedef enum { USBPAD_XBOXONE, USBPAD_XBOX360, USBPAD_PS4, USBPAD_PS3,
               USBPAD_DUALSENSE, USBPAD_SWITCHPRO } usbpad_brand_t;

typedef struct {
    u16 buttons;
    s16 lx, ly, rx, ry;
    u8  lt, rt;
} usbhid_pad_t;

typedef struct {
    u16 vid, pid;
    usbpad_brand_t brand;
    const char *name;
    s32  (*init_fn)(s32 fd, u8 out_ep);   /* NULL if no bring-up packet needed */
    void (*parse_fn)(const u8 *report, u16 len, usbhid_pad_t *out);
} usbhid_profile_t;

/* ---- Xbox One (wired) ------------------------------------------------- */

/* GIP_CMD_POWER, GIP_OPT_INTERNAL, seq=0, 1-byte payload, GIP_PWR_ON.
   Required on 2015+ firmware before the pad streams input reports.
   Verified against the current upstream drivers/input/joystick/xpad.c. */
static const u8 s_xboxone_poweron[] = { 0x05, 0x20, 0x00, 0x01, 0x00 };

static s32 XboxOne_Init(s32 fd, u8 out_ep)
{
    u8 *buf;
    s32 ret;
    if (!out_ep)
        return USB_FAILED; /* no interrupt OUT endpoint found — can't init */
    /* Heap + memalign(32), not a stack local — see the comment in
       USBHID_TryOpen() on why IPC buffers must not live on the stack. */
    buf = (u8 *)memalign(32, 32);
    if (!buf)
        return USB_FAILED;
    memcpy(buf, s_xboxone_poweron, sizeof(s_xboxone_poweron));
    ret = USB_WriteIntrMsg(fd, out_ep, sizeof(s_xboxone_poweron), buf);
    free(buf);
    return ret;
}

/* GIP_CMD_INPUT (0x20) report layout, verified against xpad.c's
   xpadone_process_packet(): buttons in data[4]/data[5], 10-bit analog
   triggers at data[6:7]/data[8:9] LE16, signed sticks at data[10:17] LE16
   (Y axes are bitwise-inverted by the firmware, matching kernel's ~value). */
static void XboxOne_Parse(const u8 *d, u16 len, usbhid_pad_t *out)
{
    u16 buttons;
    u16 lt16, rt16;

    if (len < 18 || d[0] != 0x20 /* GIP_CMD_INPUT — ignore sync/auth/etc */)
        return;

    buttons = 0;
    if (d[4] & 0x04) buttons |= USBHID_BTN_START;
    if (d[4] & 0x08) buttons |= USBHID_BTN_BACK;
    if (d[4] & 0x10) buttons |= USBHID_BTN_A;
    if (d[4] & 0x20) buttons |= USBHID_BTN_B;
    if (d[4] & 0x40) buttons |= USBHID_BTN_X;
    if (d[4] & 0x80) buttons |= USBHID_BTN_Y;
    if (d[5] & 0x01) buttons |= USBHID_BTN_DUP;
    if (d[5] & 0x02) buttons |= USBHID_BTN_DDOWN;
    if (d[5] & 0x04) buttons |= USBHID_BTN_DLEFT;
    if (d[5] & 0x08) buttons |= USBHID_BTN_DRIGHT;
    if (d[5] & 0x10) buttons |= USBHID_BTN_LB;
    if (d[5] & 0x20) buttons |= USBHID_BTN_RB;
    if (d[5] & 0x40) buttons |= USBHID_BTN_L3;
    if (d[5] & 0x80) buttons |= USBHID_BTN_R3;
    out->buttons = buttons;

    lt16 = (u16)(d[6] | (d[7] << 8));   /* 10-bit, 0..1023 */
    rt16 = (u16)(d[8] | (d[9] << 8));
    out->lt = (u8)(lt16 >> 2);
    out->rt = (u8)(rt16 >> 2);

    out->lx =  (s16)(d[10] | (d[11] << 8));
    out->ly = ~(s16)(d[12] | (d[13] << 8));
    out->rx =  (s16)(d[14] | (d[15] << 8));
    out->ry = ~(s16)(d[16] | (d[17] << 8));
}

/* ---- Xbox 360 (wired) ---------------------------------------------------
   Streams input as soon as the interrupt IN read is armed — no bring-up
   packet required like Xbox One's GIP power-on. Linux's xpad_start_input()
   additionally issues a vendor "finish init" control read after arming the
   URB; it's non-critical (some wired 360 pads stream fine without it), so
   failure here doesn't block opening the pad. */

static s32 Xbox360_Init(s32 fd, u8 out_ep)
{
    u8 *buf;
    (void)out_ep;
    buf = (u8 *)memalign(32, 32);
    if (!buf)
        return USB_OK; /* non-fatal — skip the optional vendor read */
    USB_ReadCtrlMsg(fd,
                     USB_CTRLTYPE_DIR_DEVICE2HOST | USB_CTRLTYPE_TYPE_VENDOR |
                     USB_CTRLTYPE_REC_INTERFACE,
                     0x01, 0x0100, 0x0000, 20, buf);
    free(buf);
    return USB_OK; /* non-fatal even if the vendor read above fails */
}

/* Verified against the current upstream xpad360_process_packet(): buttons in
   data[2]/data[3], single-byte 0..255 triggers at data[4]/data[5] (unlike
   Xbox One's 10-bit triggers), signed sticks at data[6:13] LE16 (Y axes
   bitwise-inverted, same convention as Xbox One). */
static void Xbox360_Parse(const u8 *d, u16 len, usbhid_pad_t *out)
{
    u16 buttons;

    if (len < 14 || d[0] != 0x00 /* normal input report */)
        return;

    buttons = 0;
    if (d[2] & 0x01) buttons |= USBHID_BTN_DUP;
    if (d[2] & 0x02) buttons |= USBHID_BTN_DDOWN;
    if (d[2] & 0x04) buttons |= USBHID_BTN_DLEFT;
    if (d[2] & 0x08) buttons |= USBHID_BTN_DRIGHT;
    if (d[2] & 0x10) buttons |= USBHID_BTN_START;
    if (d[2] & 0x20) buttons |= USBHID_BTN_BACK;
    if (d[2] & 0x40) buttons |= USBHID_BTN_L3;
    if (d[2] & 0x80) buttons |= USBHID_BTN_R3;
    if (d[3] & 0x01) buttons |= USBHID_BTN_LB;
    if (d[3] & 0x02) buttons |= USBHID_BTN_RB;
    /* d[3] bit2 = guide/Xbox button — no unified-pad equivalent, skipped */
    if (d[3] & 0x10) buttons |= USBHID_BTN_A;
    if (d[3] & 0x20) buttons |= USBHID_BTN_B;
    if (d[3] & 0x40) buttons |= USBHID_BTN_X;
    if (d[3] & 0x80) buttons |= USBHID_BTN_Y;
    out->buttons = buttons;

    out->lt = d[4];
    out->rt = d[5];

    out->lx =  (s16)(d[6]  | (d[7]  << 8));
    out->ly = ~(s16)(d[8]  | (d[9]  << 8));
    out->rx =  (s16)(d[10] | (d[11] << 8));
    out->ry = ~(s16)(d[12] | (d[13] << 8));
}

/* ---- PS4 (DualShock 4) -------------------------------------------------
   Streams standard USB HID input reports (report ID 0x01) immediately on
   connect — no bring-up packet needed, unlike PS3/Xbox One. */

static void PS4_Parse(const u8 *d, u16 len, usbhid_pad_t *out)
{
    u16 buttons;
    u8  hat;

    if (len < 10 || d[0] != 0x01)
        return;

    out->lx = (s16)(((s32)d[1] - 128) * 256);
    out->ly = (s16)(((s32)d[2] - 128) * 256);
    out->rx = (s16)(((s32)d[3] - 128) * 256);
    out->ry = (s16)(((s32)d[4] - 128) * 256);

    buttons = 0;
    hat = d[5] & 0x0F; /* standard HID hat switch: 0=up .. 7=up-left, 8=released */
    switch (hat) {
    case 0: buttons |= USBHID_BTN_DUP; break;
    case 1: buttons |= USBHID_BTN_DUP | USBHID_BTN_DRIGHT; break;
    case 2: buttons |= USBHID_BTN_DRIGHT; break;
    case 3: buttons |= USBHID_BTN_DRIGHT | USBHID_BTN_DDOWN; break;
    case 4: buttons |= USBHID_BTN_DDOWN; break;
    case 5: buttons |= USBHID_BTN_DDOWN | USBHID_BTN_DLEFT; break;
    case 6: buttons |= USBHID_BTN_DLEFT; break;
    case 7: buttons |= USBHID_BTN_DLEFT | USBHID_BTN_DUP; break;
    default: break; /* 8 = released */
    }
    if (d[5] & 0x10) buttons |= USBHID_BTN_X; /* Square  -> X */
    if (d[5] & 0x20) buttons |= USBHID_BTN_A; /* Cross   -> A */
    if (d[5] & 0x40) buttons |= USBHID_BTN_B; /* Circle  -> B */
    if (d[5] & 0x80) buttons |= USBHID_BTN_Y; /* Triangle-> Y */

    if (d[6] & 0x01) buttons |= USBHID_BTN_LB;
    if (d[6] & 0x02) buttons |= USBHID_BTN_RB;
    if (d[6] & 0x10) buttons |= USBHID_BTN_BACK;  /* Share */
    if (d[6] & 0x20) buttons |= USBHID_BTN_START; /* Options */
    if (d[6] & 0x40) buttons |= USBHID_BTN_L3;
    if (d[6] & 0x80) buttons |= USBHID_BTN_R3;
    out->buttons = buttons;

    out->lt = d[8];
    out->rt = d[9];
}

/* ---- DualSense (PS5) ----------------------------------------------------
   Streams standard USB HID input reports (report ID 0x01) immediately on
   connect, same as PS4 — no bring-up packet needed. Layout verified against
   the current upstream drivers/hid/hid-playstation.c
   (struct dualsense_input_report), which the raw report mirrors starting
   one byte after the report ID: d[1..2]=left stick, d[3..4]=right stick,
   d[5..6]=L2/R2 analog, d[7]=sequence number, d[8..10]=button bytes. */
static void DualSense_Parse(const u8 *d, u16 len, usbhid_pad_t *out)
{
    u16 buttons;
    u8  hat;

    if (len < 11 || d[0] != 0x01)
        return;

    out->lx = (s16)(((s32)d[1] - 128) * 256);
    out->ly = (s16)(((s32)d[2] - 128) * 256);
    out->rx = (s16)(((s32)d[3] - 128) * 256);
    out->ry = (s16)(((s32)d[4] - 128) * 256);
    out->lt = d[5];
    out->rt = d[6];

    buttons = 0;
    hat = d[8] & 0x0F; /* standard HID hat switch: 0=up .. 7=up-left, 8=released */
    switch (hat) {
    case 0: buttons |= USBHID_BTN_DUP; break;
    case 1: buttons |= USBHID_BTN_DUP | USBHID_BTN_DRIGHT; break;
    case 2: buttons |= USBHID_BTN_DRIGHT; break;
    case 3: buttons |= USBHID_BTN_DRIGHT | USBHID_BTN_DDOWN; break;
    case 4: buttons |= USBHID_BTN_DDOWN; break;
    case 5: buttons |= USBHID_BTN_DDOWN | USBHID_BTN_DLEFT; break;
    case 6: buttons |= USBHID_BTN_DLEFT; break;
    case 7: buttons |= USBHID_BTN_DLEFT | USBHID_BTN_DUP; break;
    default: break; /* 8 = released */
    }
    if (d[8] & 0x10) buttons |= USBHID_BTN_X; /* Square   -> X */
    if (d[8] & 0x20) buttons |= USBHID_BTN_A; /* Cross    -> A */
    if (d[8] & 0x40) buttons |= USBHID_BTN_B; /* Circle   -> B */
    if (d[8] & 0x80) buttons |= USBHID_BTN_Y; /* Triangle -> Y */

    if (d[9] & 0x01) buttons |= USBHID_BTN_LB;
    if (d[9] & 0x02) buttons |= USBHID_BTN_RB;
    /* d[9] bits 2/3 = L2/R2 digital, redundant with analog trigger threshold */
    if (d[9] & 0x10) buttons |= USBHID_BTN_BACK;  /* Create/Share */
    if (d[9] & 0x20) buttons |= USBHID_BTN_START; /* Options */
    if (d[9] & 0x40) buttons |= USBHID_BTN_L3;
    if (d[9] & 0x80) buttons |= USBHID_BTN_R3;
    out->buttons = buttons;
}

/* ---- PS3 (Sixaxis / DualShock 3) --------------------------------------- */

/* Reading these HID feature reports is what switches a freshly-connected
   Sixaxis/DS3 out of "USB charge only" mode into operational input
   streaming, mirroring Linux hid-sony.c's sixaxis_set_operational_usb().
   Report sizes (17 / 8 bytes) match SIXAXIS_REPORT_0xF2/0xF5_SIZE upstream. */
static s32 PS3_Init(s32 fd, u8 out_ep)
{
    u8 *buf;
    s32 ret;
    (void)out_ep;

    buf = (u8 *)memalign(32, 32);
    if (!buf)
        return USB_FAILED;

    ret = USB_ReadCtrlMsg(fd, USB_REQTYPE_INTERFACE_GET, USB_REQ_GETREPORT,
                           (USB_REPTYPE_FEATURE << 8) | 0xF2, 0, 17, buf);
    if (ret < 0) {
        free(buf);
        return ret;
    }

    ret = USB_ReadCtrlMsg(fd, USB_REQTYPE_INTERFACE_GET, USB_REQ_GETREPORT,
                           (USB_REPTYPE_FEATURE << 8) | 0xF5, 0, 8, buf);
    free(buf);
    return ret;
}

/* Community-reverse-engineered Sixaxis/DS3 USB input report layout.
   VERIFY ON HARDWARE: unlike the Xbox One layout above, this was not cross-
   checked against upstream driver source (hid-sony.c parses this via the
   generic HID report-descriptor path, not fixed offsets) — if a PS3 pad
   connects but inputs read wrong/garbled, dump raw bytes via wii_diag() and
   re-derive offsets from that capture. */
static void PS3_Parse(const u8 *d, u16 len, usbhid_pad_t *out)
{
    u16 buttons;

    if (len < 10 || d[0] != 0x01)
        return;

    buttons = 0;
    if (d[2] & 0x01) buttons |= USBHID_BTN_BACK;  /* Select */
    if (d[2] & 0x02) buttons |= USBHID_BTN_L3;
    if (d[2] & 0x04) buttons |= USBHID_BTN_R3;
    if (d[2] & 0x08) buttons |= USBHID_BTN_START;
    if (d[2] & 0x10) buttons |= USBHID_BTN_DUP;
    if (d[2] & 0x20) buttons |= USBHID_BTN_DRIGHT;
    if (d[2] & 0x40) buttons |= USBHID_BTN_DDOWN;
    if (d[2] & 0x80) buttons |= USBHID_BTN_DLEFT;

    if (d[3] & 0x04) buttons |= USBHID_BTN_LB;  /* L1 */
    if (d[3] & 0x08) buttons |= USBHID_BTN_RB;  /* R1 */
    if (d[3] & 0x10) buttons |= USBHID_BTN_Y;   /* Triangle */
    if (d[3] & 0x20) buttons |= USBHID_BTN_A;   /* Cross */
    if (d[3] & 0x40) buttons |= USBHID_BTN_B;   /* Circle */
    if (d[3] & 0x80) buttons |= USBHID_BTN_X;   /* Square */
    out->buttons = buttons;

    out->lx = (s16)(((s32)d[6] - 128) * 256);
    out->ly = (s16)(((s32)d[7] - 128) * 256);
    out->rx = (s16)(((s32)d[8] - 128) * 256);
    out->ry = (s16)(((s32)d[9] - 128) * 256);
    out->lt = (len > 18) ? d[18] : 0; /* L2 analog pressure — VERIFY offset */
    out->rt = (len > 19) ? d[19] : 0; /* R2 analog pressure — VERIFY offset */
}

/* ---- Nintendo Switch Pro Controller (wired) -----------------------------
   Unlike every other pad here, it does NOT stream input by default over
   USB — it needs a wake-up handshake before it leaves "USB HID-only" mode,
   then an explicit command to switch to full/standard input reports (0x30).
   Sequence and report layout verified against the current upstream
   drivers/hid/hid-nintendo.c (joycon_usb_send_handshake() /
   joycon_set_report_mode() / struct joycon_input_report). */

static const u8 s_switchpro_handshake[]  = { 0x80, 0x02 };
static const u8 s_switchpro_baudrate[]   = { 0x80, 0x03 };
static const u8 s_switchpro_no_timeout[] = { 0x80, 0x04 };

static s32 SwitchPro_Init(s32 fd, u8 out_ep)
{
    u8 *buf;
    s32 ret;

    if (!out_ep)
        return USB_FAILED;

    buf = (u8 *)memalign(32, 32);
    if (!buf)
        return USB_FAILED;

    /* USB wake-up: HANDSHAKE, BAUDRATE_3M, HANDSHAKE again, NO_TIMEOUT.
       Each is a bare 2-byte {0x80, cmd} interrupt OUT write. Only the first
       handshake is treated as fatal — the rest are best-effort, mirroring
       the Linux driver's own leniency here (baud rate / timeout tweaks
       aren't required for basic input streaming to work). */
    memcpy(buf, s_switchpro_handshake, sizeof(s_switchpro_handshake));
    ret = USB_WriteIntrMsg(fd, out_ep, sizeof(s_switchpro_handshake), buf);
    if (ret < 0) {
        free(buf);
        return ret;
    }

    memcpy(buf, s_switchpro_baudrate, sizeof(s_switchpro_baudrate));
    USB_WriteIntrMsg(fd, out_ep, sizeof(s_switchpro_baudrate), buf);

    memcpy(buf, s_switchpro_handshake, sizeof(s_switchpro_handshake));
    USB_WriteIntrMsg(fd, out_ep, sizeof(s_switchpro_handshake), buf);

    memcpy(buf, s_switchpro_no_timeout, sizeof(s_switchpro_no_timeout));
    USB_WriteIntrMsg(fd, out_ep, sizeof(s_switchpro_no_timeout), buf);

    /* Output report 0x01: packet counter, 8 bytes neutral rumble data (the
       widely-documented Joy-Con "no rumble" default — 0x00 0x01 0x40 0x40
       per side), subcommand 0x03 (set input report mode) with mode 0x30
       (full/standard: buttons + both analog sticks every packet). This one
       IS treated as fatal — without it the pad never streams real data. */
    memset(buf, 0, 32);
    buf[0]  = 0x01;
    buf[1]  = 0x00;
    buf[2]  = 0x00; buf[3] = 0x01; buf[4] = 0x40; buf[5] = 0x40;
    buf[6]  = 0x00; buf[7] = 0x01; buf[8] = 0x40; buf[9] = 0x40;
    buf[10] = 0x03;
    buf[11] = 0x30;
    ret = USB_WriteIntrMsg(fd, out_ep, 12, buf);
    free(buf);
    return ret;
}

/* Full/standard (0x30) input report: d[0]=0x30, d[3]/d[4]/d[5] = 3 button
   bytes (24 bits), d[6..8]/d[9..11] = 12-bit packed left/right stick X/Y.
   VERIFY ON HARDWARE: the button bit layout is cross-checked against
   upstream hid-nintendo.c, but the stick Y-axis sign (up = positive raw,
   like GC/DRC's hardware convention, negated below to match this port's
   "up = negative" target) was not verifiable without hardware — if up/down
   read inverted, drop the negation on ly/ry. */
static void SwitchPro_Parse(const u8 *d, u16 len, usbhid_pad_t *out)
{
    u16 buttons;
    u16 rawlx, rawly, rawrx, rawry;

    if (len < 12 || d[0] != 0x30)
        return;

    buttons = 0;
    if (d[3] & 0x08) buttons |= USBHID_BTN_A;
    if (d[3] & 0x04) buttons |= USBHID_BTN_B;
    if (d[3] & 0x02) buttons |= USBHID_BTN_X;
    if (d[3] & 0x01) buttons |= USBHID_BTN_Y;
    if (d[3] & 0x40) buttons |= USBHID_BTN_RB; /* R */
    if (d[5] & 0x40) buttons |= USBHID_BTN_LB; /* L */
    if (d[4] & 0x01) buttons |= USBHID_BTN_BACK;  /* Minus */
    if (d[4] & 0x02) buttons |= USBHID_BTN_START; /* Plus */
    if (d[4] & 0x04) buttons |= USBHID_BTN_R3;
    if (d[4] & 0x08) buttons |= USBHID_BTN_L3;
    if (d[5] & 0x01) buttons |= USBHID_BTN_DDOWN;
    if (d[5] & 0x02) buttons |= USBHID_BTN_DUP;
    if (d[5] & 0x04) buttons |= USBHID_BTN_DRIGHT;
    if (d[5] & 0x08) buttons |= USBHID_BTN_DLEFT;
    out->buttons = buttons;

    /* ZL/ZR are digital-only on Pro Controller (no analog trigger hardware)
       — map to the unified pad's analog trigger slots as full-scale digital
       so USBPad_Input_Frame's threshold-based bind still works like an
       analog pad's fully-pressed trigger. */
    out->rt = (d[3] & 0x80) ? 255 : 0; /* ZR */
    out->lt = (d[5] & 0x80) ? 255 : 0; /* ZL */

    rawlx = (u16)(d[6] | ((d[7] & 0x0F) << 8));
    rawly = (u16)((d[7] >> 4) | (d[8] << 4));
    rawrx = (u16)(d[9] | ((d[10] & 0x0F) << 8));
    rawry = (u16)((d[10] >> 4) | (d[11] << 4));

    out->lx =  (s16)(((s32)rawlx - 2048) * 16);
    out->ly = -(s16)(((s32)rawly - 2048) * 16);
    out->rx =  (s16)(((s32)rawrx - 2048) * 16);
    out->ry = -(s16)(((s32)rawry - 2048) * 16);
}

/* ---- Profile table ------------------------------------------------------
   USB_GetDeviceList(..., USB_CLASS_HID, ...) is broad (matches the Wii's
   own USB keyboard/mouse too), so VID/PID filtering against this table is
   mandatory. VERIFY AT IMPL TIME: more Xbox One hardware-revision PIDs
   exist than are listed here (Microsoft has shipped several); extend as
   needed once real hardware is available to test against. */
static const usbhid_profile_t s_profiles[] = {
    { 0x054C, 0x0268, USBPAD_PS3,     "Sixaxis/DualShock 3", PS3_Init,     PS3_Parse     },
    { 0x054C, 0x05C4, USBPAD_PS4,     "DualShock 4 (v1)",    NULL,         PS4_Parse     },
    { 0x054C, 0x09CC, USBPAD_PS4,     "DualShock 4 (v2)",    NULL,         PS4_Parse     },
    { 0x045E, 0x02D1, USBPAD_XBOXONE, "Xbox One",            XboxOne_Init, XboxOne_Parse },
    { 0x045E, 0x02DD, USBPAD_XBOXONE, "Xbox One (2015 fw)",  XboxOne_Init, XboxOne_Parse },
    { 0x045E, 0x02E3, USBPAD_XBOXONE, "Xbox One Elite",      XboxOne_Init, XboxOne_Parse },
    { 0x045E, 0x02EA, USBPAD_XBOXONE, "Xbox One S",          XboxOne_Init, XboxOne_Parse },
    { 0x045E, 0x0B12, USBPAD_XBOXONE, "Xbox One Elite 2",    XboxOne_Init, XboxOne_Parse },
    { 0x045E, 0x028E, USBPAD_XBOX360, "Xbox 360 (wired)",    Xbox360_Init, Xbox360_Parse },
    { 0x0738, 0x4716, USBPAD_XBOX360, "Mad Catz Xbox 360",   Xbox360_Init, Xbox360_Parse },
    /* Xbox Series X/S speaks the same GIP protocol as Xbox One wired (same
       power-on packet, same GIP_CMD_INPUT report layout) — reuses the Xbox
       One init/parse functions directly. */
    { 0x045E, 0x0B13, USBPAD_XBOXONE, "Xbox Series X/S",     XboxOne_Init, XboxOne_Parse },
    { 0x054C, 0x0CE6, USBPAD_DUALSENSE, "DualSense (PS5)",   NULL,         DualSense_Parse },
    { 0x057E, 0x2009, USBPAD_SWITCHPRO, "Switch Pro Controller", SwitchPro_Init, SwitchPro_Parse },
};
#define USBHID_PROFILE_COUNT (sizeof(s_profiles) / sizeof(s_profiles[0]))

/* Must cover the largest report any supported pad's interrupt IN endpoint
   can send in one packet — DS4/DualSense USB reports run up to 64 bytes.
   Real hardware testing showed the console crash right as the very first
   report arrived when this was 32: IOS's DMA into this buffer is sized by
   the endpoint's actual packet size, not by what we ask to read, so an
   undersized buffer here overflows into whatever follows it in memory the
   moment real data shows up (as opposed to the 0-byte boot-time scans,
   which never exercised this path at all). */
#define USBHID_MAX_REPORT  64
#define USBHID_MAX_ENTRIES 16

static qboolean                s_inited   = qfalse;
static qboolean                s_active   = qfalse;
static s32                     s_fd       = -1;
static const usbhid_profile_t *s_profile  = NULL;
static u8                      s_in_ep    = 0;
static u16                     s_report_len = 0;
static usbhid_pad_t            s_pad;
static u8 ATTRIBUTE_ALIGN(32)  s_raw_buf[USBHID_MAX_REPORT];

/* Set on a read error inside the async callback, consumed by USBHID_Poll()
   on the main thread — same split as the hotplug-detection design below,
   and for the same reason: USB_CloseDevice() is a blocking IOS call, and
   issuing blocking IOS calls from inside an IOS async completion callback
   is the exact category of bug that caused the original hotplug-notify
   crash. USBHID_Close() used to be called directly from the read callback;
   that was never actually verified safe on a real physical unplug and is
   the same class of risk, just not yet observed as a crash — deferring it
   here closes that gap defensively rather than waiting to find out. */
static qboolean s_close_pending = qfalse;

static void USBHID_Close(void)
{
    if (s_fd >= 0) {
        s32 fd = s_fd;
        s_fd = -1;
        USB_CloseDevice(&fd);
    }
    s_active        = qfalse;
    s_profile       = NULL;
    s_close_pending = qfalse;
    memset(&s_pad, 0, sizeof(s_pad));
}

static s32 USBHID_ReadCallback(s32 result, void *usrdata)
{
    (void)usrdata;

    /* No wii_diag()/wii_diag_sync() calls of any kind belong in this
       function — this is an IOS async completion callback, and filesystem
       I/O (fopen/fflush/fsync, all of which this project's diag helpers do)
       is exactly the kind of blocking/IPC-heavy call that has repeatedly
       proven unsafe from inside an IOS callback context on this platform
       (see USBHID_Close()'s and the hotplug callbacks' history above). A
       one-shot "first callback invocation" diagnostic marker was added
       here briefly to chase an earlier bug and turned out to BE a new
       instance of this exact category of bug — it made the console crash
       on literally every single open, 100% reproducible, until removed. */

    if (result < 0) {
        /* Flag for the main thread to close, don't touch the fd here and
           don't retry against it — deliberately NOT reissuing
           USB_ReadIntrMsgAsync or USB_CloseDevice from inside this async
           callback. A prior version retried reads a few times to ride out
           transient hiccups (one showed up mid-map-load), but on a REAL
           physical disconnect that meant repeatedly hammering an fd that
           may already be invalid at the IOS level, and that crashed the
           console on unplug on real hardware. USBHID_Poll()'s periodic
           rescan (~1x/sec) reconnects automatically once the close actually
           happens, covering the transient case too with a short gap
           instead of zero gap. No diag logging here either — see the
           comment at the top of this function. */
        s_close_pending = qtrue;
        return 0;
    }

    if (s_profile)
        s_profile->parse_fn(s_raw_buf, (u16)result, &s_pad);

    if (s_fd >= 0)
        USB_ReadIntrMsgAsync(s_fd, s_in_ep, s_report_len, s_raw_buf,
                              USBHID_ReadCallback, NULL);
    return 0;
}

/* Walk the device's interface(s) for the first interrupt IN endpoint
   (mandatory) and interrupt OUT endpoint (optional, used by Xbox One's
   bring-up packet). Endpoint numbers aren't hardcoded even though they're
   often stable, matching this port's existing defensive-probe style
   (cf. wii_input.c's WPAD_Probe + error-check-before-trust pattern).

   Deliberately does NOT filter by bInterfaceClass here: the VID/PID match
   in USBHID_TryOpen() already confirms this is a known device, and Xbox
   controllers declare a vendor-specific interface class (0xFF), not HID
   (0x03) — filtering on USB_CLASS_HID here would silently find zero
   endpoints for every Xbox pad even after it's correctly matched by PID. */
static qboolean USBHID_FindEndpoints(usb_devdesc *dd, u8 *in_ep, u8 *out_ep)
{
    int c, i, e;
    *in_ep = 0;
    *out_ep = 0;
    for (c = 0; c < dd->bNumConfigurations; c++) {
        usb_configurationdesc *cd = &dd->configurations[c];
        for (i = 0; i < cd->bNumInterfaces; i++) {
            usb_interfacedesc *id = &cd->interfaces[i];
            for (e = 0; e < id->bNumEndpoints; e++) {
                usb_endpointdesc *ep = &id->endpoints[e];
                if ((ep->bmAttributes & 0x03) != USB_ENDPOINT_INTERRUPT)
                    continue;
                if (ep->bEndpointAddress & USB_ENDPOINT_IN) {
                    if (!*in_ep)
                        *in_ep = ep->bEndpointAddress;
                } else if (!*out_ep) {
                    *out_ep = ep->bEndpointAddress;
                }
            }
        }
    }
    return (*in_ep != 0) ? qtrue : qfalse;
}

static qboolean USBHID_TryOpen(const usb_device_entry *ent)
{
    const usbhid_profile_t *prof = NULL;
    usb_devdesc *dd;
    u8 in_ep, out_ep, config_value;
    qboolean ok;
    s32 fd;
    int p;

    for (p = 0; p < (int)USBHID_PROFILE_COUNT; p++) {
        if (s_profiles[p].vid == ent->vid && s_profiles[p].pid == ent->pid) {
            prof = &s_profiles[p];
            break;
        }
    }
    if (!prof)
        return qfalse;

    /* Heap-allocated + memalign(32), NOT a stack local with an alignment
       attribute: IOS's IPC layer does raw DMA against this buffer, and a
       stack "aligned" variable only controls placement within the frame —
       it doesn't reliably guarantee the frame itself lands on a physical
       32-byte boundary across compilers/call depths. This is the same
       pattern RetroArch's wiiusb_hid.c (a real, shipped, working PPC-
       userland libogc USB HID driver) uses for every IPC buffer — matching
       it after stack-local alignment was the leading suspect for a crash
       that happened exactly at device-attach time. */
    dd = (usb_devdesc *)memalign(32, sizeof(usb_devdesc));
    if (!dd)
        return qfalse;

    /* Durable (fsync'd) markers at every blocking IOS call in this sequence —
       plain wii_diag()'s buffer can be lost if a call hangs the console, so
       these use wii_diag_sync() despite the general rule against that mid-
       frame (this runs at boot/hotplug time, not per-frame). If a hang
       recurs, whichever marker is LAST in diag.txt pinpoints the exact call. */
    wii_diag_sync("[usbhid] trying %s (vid=%04x pid=%04x)\n", prof->name, ent->vid, ent->pid);

    if (USB_OpenDevice(ent->device_id, ent->vid, ent->pid, &fd) != USB_OK) {
        free(dd);
        return qfalse;
    }
    wii_diag_sync("[usbhid] OpenDevice ok, fd=%d\n", (int)fd);

    if (USB_GetDescriptors(fd, dd) != USB_OK) {
        USB_CloseDevice(&fd);
        free(dd);
        return qfalse;
    }
    wii_diag_sync("[usbhid] GetDescriptors ok, %d config(s)\n", (int)dd->bNumConfigurations);

    ok = USBHID_FindEndpoints(dd, &in_ep, &out_ep);
    /* Grabbed only for the diag print below — read before FreeDescriptors()
       releases the configurations array. Deliberately never passed to
       USB_SetConfiguration(): real hardware testing showed that call
       consistently rejected on every tested pad (DS4, Xbox One), blocking
       every open before it ever reached the brand init step. RetroArch's
       wiiusb_hid.c (a real, shipped, working PPC-userland libogc USB HID
       driver) never calls it either — IOS's own automatic device
       enumeration apparently already configures the device before
       application code ever gets a device_id for it. */
    config_value = (dd->bNumConfigurations > 0) ? dd->configurations[0].bConfigurationValue : 0;
    USB_FreeDescriptors(dd);
    free(dd);

    if (!ok) {
        USB_CloseDevice(&fd);
        return qfalse;
    }
    wii_diag_sync("[usbhid] endpoints in=0x%02x out=0x%02x config=%d\n",
                  in_ep, out_ep, (int)config_value);

    if (prof->init_fn && prof->init_fn(fd, out_ep) < 0) {
        wii_diag_sync("[usbhid] init failed for %s\n", prof->name);
        USB_CloseDevice(&fd);
        return qfalse;
    }
    wii_diag_sync("[usbhid] init_fn ok (or none needed)\n");

    s_fd         = fd;
    s_profile    = prof;
    s_in_ep      = in_ep;
    s_report_len = USBHID_MAX_REPORT;
    s_active     = qtrue;
    memset(&s_pad, 0, sizeof(s_pad));

    wii_diag_sync("[usbhid] opened %s (vid=%04x pid=%04x), arming read\n",
                  prof->name, ent->vid, ent->pid);

    USB_ReadIntrMsgAsync(s_fd, s_in_ep, s_report_len, s_raw_buf,
                          USBHID_ReadCallback, NULL);
    wii_diag_sync("[usbhid] read armed\n");
    return qtrue;
}

/* Xbox 360/One/Series controllers declare a vendor-specific USB interface
   class, not HID — this is exactly why they need a dedicated driver (xpad)
   on every OS instead of riding the generic HID stack. A USB_CLASS_HID-only
   scan silently never finds them even with a correct VID/PID table entry,
   so every scan (initial + hotplug) must also check this class. */
#define USBHID_VENDOR_SPECIFIC_CLASS 0xFF

static qboolean USBHID_ScanClass(u8 interface_class)
{
    usb_device_entry *entries;
    u8 cnt = 0;
    int i;
    qboolean found = qfalse;

    /* Heap + memalign(32) — see the comment in USBHID_TryOpen() on why this
       must not be a stack local, even with an alignment attribute. */
    entries = (usb_device_entry *)memalign(32, sizeof(usb_device_entry) * USBHID_MAX_ENTRIES);
    if (!entries)
        return qfalse;

    wii_diag_sync("[usbhid] ScanClass(0x%02x) calling USB_GetDeviceList\n", interface_class);
    if (USB_GetDeviceList(entries, USBHID_MAX_ENTRIES, interface_class, &cnt) != USB_OK) {
        wii_diag_sync("[usbhid] ScanClass(0x%02x) GetDeviceList failed\n", interface_class);
        free(entries);
        return qfalse;
    }
    wii_diag_sync("[usbhid] ScanClass(0x%02x) found %d device(s)\n", interface_class, (int)cnt);

    for (i = 0; i < (int)cnt; i++) {
        wii_diag_sync("[usbhid]   entry %d: vid=%04x pid=%04x device_id=%d\n",
                      i, entries[i].vid, entries[i].pid, (int)entries[i].device_id);
        if (USBHID_TryOpen(&entries[i])) {
            found = qtrue;
            break;
        }
    }
    free(entries);
    return found;
}

/* Single-pad mode: if a pad is already open, do nothing; otherwise scan and
   open the first recognized match (others are left untouched — not opened,
   not closed). */
static void USBHID_Scan(void)
{
    if (s_active)
        return;
    if (USBHID_ScanClass(USB_CLASS_HID))
        return;
    USBHID_ScanClass(USBHID_VENDOR_SPECIFIC_CLASS);
}

/* Hotplug detection is plain periodic polling of USB_GetDeviceList() from
   the main thread — deliberately NOT USB_DeviceChangeNotifyAsync().
   On real hardware, every device that ever matched this port's profile
   table (PS3/PS4/Xbox One/Xbox Series) froze the whole console on physical
   attach while the notify watch was registered — before USBHID_TryOpen()
   ever logged its first line, before the notify callback itself ever
   logged, i.e. inside IOS/libogc's own async attach-notify machinery, not
   in any code this port controls. A mass-storage-class device (never
   matching the watched HID/vendor classes at all) attached safely under
   the same watch, confirming it's specific to devices IOS actually routes
   through that notify path — not a general "any device" issue, and not
   fixable by changing what this port's callback does with the result.
   USB_GetDeviceList() itself has run safely in every single test so far
   (found 0/found N devices, no crash from the call itself) — only the
   async notify machinery is implicated. Poll it on a timer instead. */
#define USBHID_POLL_INTERVAL_FRAMES 60   /* ~1s at 60fps; hotplug isn't latency-sensitive */
static int s_poll_countdown = 0;

void USBHID_Init(void)
{
    if (s_inited)
        return;

    s_active  = qfalse;
    s_fd      = -1;
    s_profile = NULL;
    memset(&s_pad, 0, sizeof(s_pad));

    wii_diag_sync("[usbhid] USBHID_Init: calling USB_Initialize\n");
    if (USB_Initialize() != USB_OK) {
        wii_diag_sync("[usbhid] USB_Initialize failed\n");
        return;
    }
    s_inited = qtrue;
    wii_diag_sync("[usbhid] USB_Initialize ok, starting initial scan\n");

    USBHID_Scan(); /* catches a pad already plugged in at boot */
    wii_diag_sync("[usbhid] initial scan done, polling for hotplug\n");
    s_poll_countdown = USBHID_POLL_INTERVAL_FRAMES;
}

void USBHID_Shutdown(void)
{
    if (!s_inited)
        return;
    USBHID_Close();
    s_inited = qfalse;
}

/* Call once per engine frame from the main thread. Throttled to
   USBHID_POLL_INTERVAL_FRAMES so a full USB_GetDeviceList() scan (two
   classes, each a real IOS round-trip) doesn't run 60 times a second for
   no reason — hotplug detection has no latency requirement. */
void USBHID_Poll(void)
{
    if (!s_inited)
        return;

    if (s_close_pending) {
        USBHID_Close();
        /* rescan immediately after a close instead of waiting out the
           remainder of the poll interval — a real unplug should free the
           slot right away, and a replug shouldn't feel throttled. */
        s_poll_countdown = 0;
    }

    if (s_active)
        return;
    if (--s_poll_countdown > 0)
        return;
    s_poll_countdown = USBHID_POLL_INTERVAL_FRAMES;
    USBHID_Scan();
}

qboolean USBHID_Active(void)
{
    return s_active;
}

u16 USBHID_GetButtonMask(void)
{
    return s_active ? s_pad.buttons : 0;
}

void USBHID_GetAxes(s16 *lx, s16 *ly, s16 *rx, s16 *ry, u8 *lt, u8 *rt)
{
    if (!s_active) {
        *lx = *ly = *rx = *ry = 0;
        *lt = *rt = 0;
        return;
    }
    *lx = s_pad.lx; *ly = s_pad.ly; *rx = s_pad.rx; *ry = s_pad.ry;
    *lt = s_pad.lt; *rt = s_pad.rt;
}
