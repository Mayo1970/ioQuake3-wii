/* Wii controller input declarations. */
#pragma once
#include "qcommon/q_shared.h"

void     Wii_Input_Init(void);
void     Wii_Input_SetCvars(void);
void     Wii_Input_Frame(void);
qboolean Wii_Input_HomePressed(void);
