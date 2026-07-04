#pragma once
/* Wired USB HID gamepad support (Xbox One / PS4 / PS3) — raw libogc USB
   stack, no extra library dependency. See wii_usb_hid.c for the protocol
   notes and CLAUDE.md for the design rationale (single active pad, top
   input-cascade priority alongside DRC). */
#include "qcommon/q_shared.h"
#include <gctypes.h>

/* Unified virtual-gamepad button bits. Each supported brand's raw HID
   report is normalized into this brand-independent layout by its parser in
   wii_usb_hid.c, so bindings behave the same regardless of which pad is
   plugged in. */
#define USBHID_BTN_A       0x0001
#define USBHID_BTN_B       0x0002
#define USBHID_BTN_X       0x0004
#define USBHID_BTN_Y       0x0008
#define USBHID_BTN_LB      0x0010
#define USBHID_BTN_RB      0x0020
#define USBHID_BTN_BACK    0x0040
#define USBHID_BTN_START   0x0080
#define USBHID_BTN_L3      0x0100
#define USBHID_BTN_R3      0x0200
#define USBHID_BTN_DUP     0x0400
#define USBHID_BTN_DDOWN   0x0800
#define USBHID_BTN_DLEFT   0x1000
#define USBHID_BTN_DRIGHT  0x2000

void     USBHID_Init(void);
void     USBHID_Shutdown(void);
/* Must be called once per engine frame from the main thread. Internally
   throttled — actually rescans for a hotplugged pad only once every ~1s,
   not every frame. Deliberately polling-based rather than notify-based; see
   wii_usb_hid.c for why USB_DeviceChangeNotifyAsync() was dropped. */
void     USBHID_Poll(void);
qboolean USBHID_Active(void);

/* Read the latest cached pad state — safe to call every engine frame, never
   touches USB hardware directly (the async read callback updates the cache
   off the main thread's critical path). Sticks are -32767..32767 (0 =
   centered); triggers are 0..255 analog. */
u16  USBHID_GetButtonMask(void);
void USBHID_GetAxes(s16 *lx, s16 *ly, s16 *rx, s16 *ry, u8 *lt, u8 *rt);
