/* Wired USB HID gamepads, raw libogc ogc/usb.h, no library, single pad at a
   time. Offsets are Linux xpad.c/hid-sony.c ported by hand - PS3's are
   community-reversed, not upstream-verified; garbled input means re-dump it. */

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

/* GIP power-on packet - 2015+ Xbox One firmware just sits there mute
   without it. Verified against upstream xpad.c. */
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

/* GIP_CMD_INPUT layout per xpad.c's xpadone_process_packet(); Y axes are
   bitwise-inverted by the firmware, same as the kernel's ~value. */
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
   Streams as soon as the read is armed, no GIP power-on needed. The vendor
   "finish init" read below is best-effort, per Linux's xpad_start_input(). */

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

/* Per upstream xpad360_process_packet(). Triggers are plain 0..255 bytes here,
   unlike Xbox One's 10-bit ones - naturally, nothing about this stays consistent. */
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
   Streams standard HID reports immediately, no bring-up packet needed. */

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
   Same deal as PS4, no bring-up packet. Layout per upstream
   hid-playstation.c's dualsense_input_report, shifted one byte for report ID. */
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

/* Without reading these feature reports the Sixaxis/DS3 just sits in
   "USB charge only" mode forever. Mirrors hid-sony.c's operational-usb dance. */
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

/* Community-reversed layout, NOT cross-checked against upstream (hid-sony.c
   uses the generic report-descriptor path). Garbled input? Dump raw bytes and re-derive. */
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
   Only pad here that stays mute until you hand-hold it through a wake-up
   handshake, then a mode switch to 0x30. Verified against hid-nintendo.c. */

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

    /* Handshake, baud, handshake again, no-timeout - only the first is fatal,
       rest best-effort per the Linux driver's own shrug about it. */
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

    /* Report 0x01, subcommand 0x03 mode 0x30 - THIS one is fatal, skip it
       and the pad just sits there looking plugged in and doing nothing. */
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

/* 0x30 report, 12-bit packed sticks. Button bits match hid-nintendo.c; the
   stick Y negation below is unverified on real hardware - drop it if inverted. */
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

    /* ZL/ZR have no analog hardware - fake full-scale so the threshold bind
       in USBPad_Input_Frame still treats them like a real trigger pull. */
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
   USB_CLASS_HID matches the Wii's own USB keyboard/mouse too, so VID/PID
   filtering here is mandatory, not optional. More Xbox PIDs surely exist. */
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
    /* Series X/S speaks identical GIP - reuses Xbox One's functions as-is. */
    { 0x045E, 0x0B13, USBPAD_XBOXONE, "Xbox Series X/S",     XboxOne_Init, XboxOne_Parse },
    { 0x054C, 0x0CE6, USBPAD_DUALSENSE, "DualSense (PS5)",   NULL,         DualSense_Parse },
    { 0x057E, 0x2009, USBPAD_SWITCHPRO, "Switch Pro Controller", SwitchPro_Init, SwitchPro_Parse },
};
#define USBHID_PROFILE_COUNT (sizeof(s_profiles) / sizeof(s_profiles[0]))

/* Was 32, crashed the console the instant a real 64-byte DS4 report landed -
   IOS DMAs by the endpoint's packet size, not by what we asked for. */
#define USBHID_MAX_REPORT  64
#define USBHID_MAX_ENTRIES 16

static qboolean                s_inited   = qfalse;
static qboolean                s_active   = qfalse;
/* volatile: written on the main thread, read inside the IOS read callback. */
static volatile s32            s_fd       = -1;
static const usbhid_profile_t *s_profile  = NULL;
static u8                      s_in_ep    = 0;
static u16                     s_report_len = 0;
static usbhid_pad_t            s_pad;
static u8 ATTRIBUTE_ALIGN(32)  s_raw_buf[USBHID_MAX_REPORT];

/* s_pad is written by the callback; main-thread reads go through this
   volatile view so the compiler can never cache them across frames
   (plain field reads only work today via call-boundary reloads - LTO or
   inlining would break that silently). */
#define S_PAD_V (*(volatile usbhid_pad_t *)&s_pad)

/* Set inside the async read callback, consumed by USBHID_Poll() on the main
   thread. Never call the blocking USB_CloseDevice() from inside a callback. */
static volatile qboolean s_close_pending = qfalse;

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

    /* NO wii_diag()/wii_diag_sync() in this function, ever - this is an IOS
       callback, and a diag call here once crashed the console on every
       single connection until I figured out I'd added the bug while
       debugging the bug. */

    if (result < 0) {
        /* Just flag it - retrying the read here against a real unplugged fd
           crashed the console. USBHID_Poll()'s rescan picks it back up. */
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

/* Finds the first interrupt IN/OUT endpoints. Deliberately does NOT filter by
   bInterfaceClass - Xbox pads are vendor-class 0xFF, not HID, and that
   filter would silently zero out endpoints for every one of them. */
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

    /* memalign(32) heap, NEVER a stack local even with an alignment
       attribute - IOS's IPC layer DMAs this raw, and stack "alignment"
       doesn't survive across call depths. Cost a crash at attach time to learn. */
    dd = (usb_devdesc *)memalign(32, sizeof(usb_devdesc));
    if (!dd)
        return qfalse;

    /* wii_diag_sync (not the usual mid-frame-unsafe wii_diag) at every IOS
       call here - runs at boot/hotplug, not per-frame, so a fsync is fine. */
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
    /* Diag-print only, NEVER pass to USB_SetConfiguration() - every real pad
       tested (DS4, Xbox One) rejected that call outright. IOS already
       configures the device before we ever see a device_id. */
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

/* Xbox pads are vendor-class, not HID - same reason Linux needs a dedicated
   xpad driver instead of hid-generic. Scan both classes or find nothing. */
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

/* Single-pad mode: first recognized match wins, everything else left alone. */
static void USBHID_Scan(void)
{
    if (s_active)
        return;
    if (USBHID_ScanClass(USB_CLASS_HID))
        return;
    USBHID_ScanClass(USBHID_VENDOR_SPECIFIC_CLASS);
}

/* Deliberately polling, NOT USB_DeviceChangeNotifyAsync() - the async notify
   watch froze the console on attach for every matched pad, dead inside
   libogc's own machinery before a single line of my code ran. Not fixable here. */
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

/* Once per frame. Throttled - a full two-class scan is a real IOS round-trip
   each time, and hotplug detection doesn't need 60Hz. */
void USBHID_Poll(void)
{
    if (!s_inited)
        return;

    if (s_close_pending) {
        USBHID_Close();
        /* Rescan right away - a replug shouldn't feel throttled. */
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
    return s_active ? S_PAD_V.buttons : 0;
}

void USBHID_GetAxes(s16 *lx, s16 *ly, s16 *rx, s16 *ry, u8 *lt, u8 *rt)
{
    if (!s_active) {
        *lx = *ly = *rx = *ry = 0;
        *lt = *rt = 0;
        return;
    }
    *lx = S_PAD_V.lx; *ly = S_PAD_V.ly; *rx = S_PAD_V.rx; *ry = S_PAD_V.ry;
    *lt = S_PAD_V.lt; *rt = S_PAD_V.rt;
}
