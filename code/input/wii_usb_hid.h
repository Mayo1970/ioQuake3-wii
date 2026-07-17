#pragma once
/* Wired USB HID gamepads, raw libogc, no extra library. See wii_usb_hid.c
   for the protocol notes and every hardware lesson that produced them. */
#include "qcommon/q_shared.h"
#include <gctypes.h>

/* Unified button bits - every brand's parser normalizes into this so
   bindings don't care which pad is plugged in. */
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
/* Call once per frame; internally throttled to ~1/sec. Polling-based on
   purpose - see wii_usb_hid.c for why the notify-based API freezes the console. */
void     USBHID_Poll(void);
qboolean USBHID_Active(void);

/* Cached state only - never touches USB hardware directly. Sticks
   -32767..32767, triggers 0..255. */
u16  USBHID_GetButtonMask(void);
void USBHID_GetAxes(s16 *lx, s16 *ly, s16 *rx, s16 *ry, u8 *lt, u8 *rt);
