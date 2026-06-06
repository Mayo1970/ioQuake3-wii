#pragma once
#include "qcommon/q_shared.h"

void Wii_Snd_Init(void);
void Wii_Snd_Shutdown(void);

qboolean Wii_Snd_SNDDMA_Init(void);
int      Wii_Snd_SNDDMA_GetDMAPos(void);
void     Wii_Snd_SNDDMA_BeginPainting(void);
void     Wii_Snd_SNDDMA_Submit(void);
void     Wii_Snd_SNDDMA_Shutdown(void);
