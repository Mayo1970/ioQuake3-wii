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

/* Set by the boot prompt before Wii_GX_Init(). Plain global, not a cvar -
   the cvar system isn't up yet at this point in boot. */
extern int  wii_video_mode_choice;
