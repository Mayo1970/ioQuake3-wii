/* GX window / surface management declarations. */
#pragma once
#include <gccore.h>
#include "qcommon/q_shared.h"

qboolean    Wii_GX_Init(void);
void        Wii_GX_EndFrame(void);
void        Wii_GX_Shutdown(void);
GXRModeObj *Wii_GX_GetRMode(void);
int         Wii_GX_GetEFBHeight(void);
int         Wii_GX_GetXFBHeight(void);

/* Height the engine sees under native GX in every mode. 240p/264p draw it into fewer EFB lines
   (tr_gx.c), so cgame/UI keep a 4:3 FOV and layout instead of a 640x240 "widescreen" one. */
#define WII_GX_LOGICAL_HEIGHT 480

/* Set by the boot prompt before Wii_GX_Init(). Plain global, not a cvar -
   the cvar system isn't up yet at this point in boot. */
extern int  wii_video_mode_choice;

/* The boot console's XFB (wii_main.c), MEM_K0_TO_K1-mapped, sized for VIDEO_GetPreferredMode().
   Wii_GX_Init() reuses it as a render buffer in the default mode (no third ~1 MB XFB). */
void       *Wii_Console_GetFramebuffer(void);
