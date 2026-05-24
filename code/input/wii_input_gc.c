/* GameCube controller input — dual-stick FPS layout */

#include <gccore.h>
#include <ogc/pad.h>
#include <string.h>
#include <stdio.h>

#include "wii_input.h"

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "keycodes.h"

extern int  Key_GetCatcher(void);
extern void Key_SetBinding(int keynum, const char *binding);

#define GC_PORT            PAD_CHAN0
#define STICK_DEADZONE     20
#define TRIGGER_THRESHOLD  100
#define MENU_SENSITIVITY_F 2.0f

/* Axis indices — left stick: 0/1, right stick (C-stick): 4/3 */
#define AXIS_SIDE     0
#define AXIS_FORWARD  1
#define AXIS_YAW      4
#define AXIS_PITCH    3

/* GC stick raw +-127 -> Q3 joystick +-32767 */
#define GC_AXIS_SCALE  258

typedef struct { u32 bit; int q3key; } gc_btn_t;

static const gc_btn_t s_gc_buttons[] = {
    { PAD_BUTTON_A,      K_JOY1  },
    { PAD_BUTTON_B,      K_JOY2  },
    { PAD_BUTTON_X,      K_JOY3  },
    { PAD_BUTTON_Y,      K_JOY4  },
    { PAD_TRIGGER_Z,     K_JOY5  },
    { PAD_BUTTON_START,  K_JOY6  },
    { PAD_BUTTON_UP,     K_JOY7  },
    { PAD_BUTTON_DOWN,   K_JOY8  },
    { PAD_BUTTON_LEFT,   K_JOY9  },
    { PAD_BUTTON_RIGHT,  K_JOY10 },
};
#define GC_BTN_COUNT (sizeof(s_gc_buttons) / sizeof(s_gc_buttons[0]))

#define K_JOY_LTRIG  K_JOY11
#define K_JOY_RTRIG  K_JOY12

typedef struct { u32 bit; int q3key; } menu_btn_t;

static const menu_btn_t s_menu_buttons[] = {
    { PAD_BUTTON_A,      K_ENTER      },
    { PAD_BUTTON_B,      K_ESCAPE     },
    { PAD_BUTTON_X,      K_MOUSE1     },
    { PAD_BUTTON_Y,      K_CONSOLE    },
    { PAD_BUTTON_START,  K_ESCAPE     },
    { PAD_BUTTON_UP,     K_UPARROW    },
    { PAD_BUTTON_DOWN,   K_DOWNARROW  },
    { PAD_BUTTON_LEFT,   K_LEFTARROW  },
    { PAD_BUTTON_RIGHT,  K_RIGHTARROW },
};
#define MENU_BTN_COUNT (sizeof(s_menu_buttons) / sizeof(s_menu_buttons[0]))

typedef struct {
    qboolean key_held[256];
} input_state_t;

static input_state_t  s_input;
static qboolean       s_in_game       = qfalse;
static float          s_accum_x       = 0.0f;
static float          s_accum_y       = 0.0f;
static float          s_accum_cx      = 0.0f;
static float          s_accum_cy      = 0.0f;
static short          s_old_axis[4];
static qboolean       s_old_ltrig     = qfalse;
static qboolean       s_old_rtrig     = qfalse;
static qboolean       s_bindings_set  = qfalse;

static void InjectKey(int q3key, qboolean down)
{
    if (q3key < 0 || q3key >= 256)
        return;
    if (s_input.key_held[q3key] == down)
        return;
    s_input.key_held[q3key] = down;
    Com_QueueEvent(0, SE_KEY, q3key, down, 0, NULL);
}

static void ReleaseAllKeys(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        if (s_input.key_held[i]) {
            s_input.key_held[i] = qfalse;
            Com_QueueEvent(0, SE_KEY, i, qfalse, 0, NULL);
        }
    }
    s_accum_x  = s_accum_y  = 0.0f;
    s_accum_cx = s_accum_cy = 0.0f;
    s_old_axis[0] = s_old_axis[1] = s_old_axis[2] = s_old_axis[3] = 0;
}

static short GC_FilterAxis(s8 raw, int deadzone)
{
    int v = (int)raw;
    if (v > -deadzone && v < deadzone)
        return 0;
    if (v > 127)  v = 127;
    if (v < -127) v = -127;
    return (short)(v * GC_AXIS_SCALE);
}

/* Float accumulator so fractional pixels carry between frames */
static void InjectCursorStick(s8 x, s8 y, float sensitivity,
                               float *ax, float *ay)
{
    int ix = (int)x;
    int iy = (int)y;
    if (ix > -STICK_DEADZONE && ix < STICK_DEADZONE) ix = 0;
    if (iy > -STICK_DEADZONE && iy < STICK_DEADZONE) iy = 0;
    if (ix == 0 && iy == 0) { *ax = *ay = 0.0f; return; }

    *ax += ((float)ix / 127.0f) * sensitivity;
    *ay += ((float)iy / 127.0f) * sensitivity;

    int out_x = (int)*ax;
    int out_y = (int)*ay;
    *ax -= (float)out_x;
    *ay -= (float)out_y;

    if (out_x != 0 || out_y != 0)
        Com_QueueEvent(0, SE_MOUSE, out_x, -out_y, 0, NULL);
}

static void SetDefaultBindings(void)
{
    if (s_bindings_set)
        return;
    s_bindings_set = qtrue;

    Key_SetBinding(K_JOY1,      "+moveup");     /* A = jump */
    Key_SetBinding(K_JOY2,      "+movedown");   /* B = crouch */
    Key_SetBinding(K_JOY3,      "weapprev");    /* X = prev weapon */
    Key_SetBinding(K_JOY4,      "weapnext");    /* Y = next weapon */
    Key_SetBinding(K_JOY5,      "+zoom");       /* Z = zoom */
    Key_SetBinding(K_JOY6,      "togglemenu");  /* Start */
    Key_SetBinding(K_JOY7,      "+scores");     /* D-pad up */
    Key_SetBinding(K_JOY8,      "+attack");     /* D-pad down */
    Key_SetBinding(K_JOY9,      "weapprev");    /* D-pad left */
    Key_SetBinding(K_JOY10,     "weapnext");    /* D-pad right */
    Key_SetBinding(K_JOY_LTRIG, "+speed");      /* L trigger = walk */
    Key_SetBinding(K_JOY_RTRIG, "+attack");     /* R trigger = fire */
}

void Wii_Input_Init(void)
{
    memset(&s_input, 0, sizeof(s_input));
    s_in_game      = qfalse;
    s_bindings_set = qfalse;
    s_accum_x = s_accum_y = 0.0f;
    s_accum_cx = s_accum_cy = 0.0f;
    s_old_ltrig = s_old_rtrig = qfalse;
    memset(s_old_axis, 0, sizeof(s_old_axis));
    PAD_Init();
#ifdef WII_DEBUG
    printf("[input] GameCube PAD initialised (dual-stick)\n");
#endif
}

void Wii_Input_Frame(void)
{
    int i;

    PAD_ScanPads();

    u32 held  = PAD_ButtonsHeld(GC_PORT);
    s8  lx    = PAD_StickX(GC_PORT);
    s8  ly    = PAD_StickY(GC_PORT);
    s8  cx    = PAD_SubStickX(GC_PORT);
    s8  cy    = PAD_SubStickY(GC_PORT);
    u8  l_ana = PAD_TriggerL(GC_PORT);
    u8  r_ana = PAD_TriggerR(GC_PORT);

    qboolean in_game = (Key_GetCatcher() == 0) ? qtrue : qfalse;

    if (in_game != s_in_game) {
        ReleaseAllKeys();
        s_in_game = in_game;
    }

    if (!in_game) {
        for (i = 0; i < (int)MENU_BTN_COUNT; i++)
            InjectKey(s_menu_buttons[i].q3key,
                      (held & s_menu_buttons[i].bit) ? qtrue : qfalse);

        InjectKey(K_MOUSE1, r_ana > TRIGGER_THRESHOLD ? qtrue : qfalse);

        InjectCursorStick(lx, ly, MENU_SENSITIVITY_F,
                          &s_accum_x, &s_accum_y);
        InjectCursorStick(cx, cy, MENU_SENSITIVITY_F,
                          &s_accum_cx, &s_accum_cy);
    } else {
        SetDefaultBindings();

        for (i = 0; i < (int)GC_BTN_COUNT; i++)
            InjectKey(s_gc_buttons[i].q3key,
                      (held & s_gc_buttons[i].bit) ? qtrue : qfalse);

        qboolean l_pressed = l_ana > TRIGGER_THRESHOLD ? qtrue : qfalse;
        qboolean r_pressed = r_ana > TRIGGER_THRESHOLD ? qtrue : qfalse;

        if (l_pressed != s_old_ltrig) {
            s_old_ltrig = l_pressed;
            Com_QueueEvent(0, SE_KEY, K_JOY_LTRIG, l_pressed, 0, NULL);
        }
        if (r_pressed != s_old_rtrig) {
            s_old_rtrig = r_pressed;
            Com_QueueEvent(0, SE_KEY, K_JOY_RTRIG, r_pressed, 0, NULL);
        }

        short ax_lx = GC_FilterAxis(lx, STICK_DEADZONE);
        short ax_ly = GC_FilterAxis(ly, STICK_DEADZONE);
        short ax_cx = GC_FilterAxis(cx, STICK_DEADZONE);
        short ax_cy = GC_FilterAxis(cy, STICK_DEADZONE);

        if (ax_lx != s_old_axis[0]) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_SIDE, ax_lx, 0, NULL);
            s_old_axis[0] = ax_lx;
        }
        /* Negate Y: GC stick-up = positive, Q3 forward = negative */
        short neg_ly = -ax_ly;
        if (neg_ly != s_old_axis[1]) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_FORWARD, neg_ly, 0, NULL);
            s_old_axis[1] = neg_ly;
        }
        if (ax_cx != s_old_axis[2]) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_YAW, ax_cx, 0, NULL);
            s_old_axis[2] = ax_cx;
        }
        short neg_cy = -ax_cy;
        if (neg_cy != s_old_axis[3]) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_PITCH, neg_cy, 0, NULL);
            s_old_axis[3] = neg_cy;
        }
    }
}

qboolean Wii_Input_HomePressed(void)
{
    return qfalse;
}
