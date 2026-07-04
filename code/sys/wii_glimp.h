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

/* Boot-time video mode choice, set by Wii_VideoModeBootPrompt() (wii_main.c)
   before Wii_GX_Init() runs: 0=default (VIDEO_GetPreferredMode), 1=240p NTSC,
   2=264p PAL. Plain global, not a cvar - Wii_GX_Init() runs before Com_Init. */
extern int  wii_video_mode_choice;
