/* wii_input.c -- Unified controller input (GC pad + optional Wiimote+Nunchuk)
   plus USB keyboard and mouse support. */

#ifndef WPAD_ENABLED
#define WPAD_ENABLED  0
#endif

#include <gccore.h>
#include <ogc/pad.h>
#include <ogc/usbmouse.h>
#include <wiikeyboard/keyboard.h>
#if WPAD_ENABLED
#include <wiiuse/wpad.h>
#endif
#include <string.h>
#include <stdio.h>

#include "wii_input.h"
#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "keycodes.h"

extern int  Key_GetCatcher(void);
extern void Key_SetBinding(int keynum, const char *binding);

#define STICK_DEADZONE       20   /* left stick */
#define CSTICK_DEADZONE      50   /* C-stick — wider to cover hardware drift */
#define MENU_SENSITIVITY_F   2.0f
#define TRIGGER_THRESHOLD    100

/* Axis indices — left stick: 0/1, right stick (C-stick): 4/3 */
#define AXIS_SIDE     0
#define AXIS_FORWARD  1
#define AXIS_YAW      4
#define AXIS_PITCH    3

/* GC stick +-127 -> Q3 joystick +-32767 */
#define GC_AXIS_SCALE  258

typedef struct { u32 bit; int q3key; } btn_map_t;

/* ---------- GC controller tables ---------- */

static const btn_map_t s_gc_buttons[] = {
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

static const btn_map_t s_gc_menu_buttons[] = {
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
#define GC_MENU_BTN_COUNT (sizeof(s_gc_menu_buttons) / sizeof(s_gc_menu_buttons[0]))

/* ---------- Wiimote+Nunchuk tables ---------- */

#if WPAD_ENABLED

/* Wiimote in-game buttons */
static const btn_map_t s_wm_buttons[] = {
    { WPAD_BUTTON_B,              K_JOY1  },   /* trigger = fire */
    { WPAD_BUTTON_A,              K_JOY2  },   /* A = jump */
    { WPAD_NUNCHUK_BUTTON_Z,      K_JOY3  },   /* Z = zoom */
    { WPAD_NUNCHUK_BUTTON_C,      K_JOY4  },   /* C = crouch */
    { WPAD_BUTTON_PLUS,           K_JOY5  },   /* + = menu */
    { WPAD_BUTTON_MINUS,          K_JOY6  },   /* - = scores */
    { WPAD_BUTTON_UP,             K_JOY7  },   /* D-up = weapnext */
    { WPAD_BUTTON_DOWN,           K_JOY8  },   /* D-down = weapprev */
    { WPAD_BUTTON_LEFT,           K_JOY9  },   /* D-left = weapprev (alt) */
    { WPAD_BUTTON_RIGHT,          K_JOY10 },   /* D-right = weapnext (alt) */
    { WPAD_BUTTON_1,              K_JOY11 },   /* 1 = walk */
};
#define WM_BTN_COUNT (sizeof(s_wm_buttons) / sizeof(s_wm_buttons[0]))

static const btn_map_t s_wm_menu_buttons[] = {
    { WPAD_BUTTON_A,              K_ENTER      },
    { WPAD_BUTTON_B,              K_ESCAPE     },
    { WPAD_BUTTON_PLUS,           K_ESCAPE     },
    { WPAD_BUTTON_1,              K_MOUSE1     },
    { WPAD_BUTTON_UP,             K_UPARROW    },
    { WPAD_BUTTON_DOWN,           K_DOWNARROW  },
    { WPAD_BUTTON_LEFT,           K_LEFTARROW  },
    { WPAD_BUTTON_RIGHT,          K_RIGHTARROW },
};
#define WM_MENU_BTN_COUNT (sizeof(s_wm_menu_buttons) / sizeof(s_wm_menu_buttons[0]))

/* Classic Controller in-game buttons */
static const btn_map_t s_cc_buttons[] = {
    { WPAD_CLASSIC_BUTTON_ZR,      K_JOY1  },   /* ZR = fire */
    { WPAD_CLASSIC_BUTTON_A,       K_JOY2  },   /* A = jump */
    { WPAD_CLASSIC_BUTTON_B,       K_JOY3  },   /* B = crouch */
    { WPAD_CLASSIC_BUTTON_ZL,      K_JOY4  },   /* ZL = zoom */
    { WPAD_CLASSIC_BUTTON_X,       K_JOY5  },   /* X = weapprev */
    { WPAD_CLASSIC_BUTTON_Y,       K_JOY6  },   /* Y = weapnext */
    { WPAD_CLASSIC_BUTTON_FULL_L,  K_JOY7  },   /* L = walk */
    { WPAD_CLASSIC_BUTTON_FULL_R,  K_JOY8  },   /* R = use */
    { WPAD_CLASSIC_BUTTON_PLUS,    K_JOY9  },   /* + = menu */
    { WPAD_CLASSIC_BUTTON_MINUS,   K_JOY10 },   /* - = scores */
    { WPAD_CLASSIC_BUTTON_UP,      K_JOY11 },   /* D-up = weapnext */
    { WPAD_CLASSIC_BUTTON_DOWN,    K_JOY12 },   /* D-down = weapprev */
    { WPAD_CLASSIC_BUTTON_LEFT,    K_JOY13 },   /* D-left = spare */
    { WPAD_CLASSIC_BUTTON_RIGHT,   K_JOY14 },   /* D-right = spare */
};
#define CC_BTN_COUNT (sizeof(s_cc_buttons) / sizeof(s_cc_buttons[0]))

static const btn_map_t s_cc_menu_buttons[] = {
    { WPAD_CLASSIC_BUTTON_A,       K_ENTER      },
    { WPAD_CLASSIC_BUTTON_B,       K_ESCAPE     },
    { WPAD_CLASSIC_BUTTON_PLUS,    K_ESCAPE     },
    { WPAD_CLASSIC_BUTTON_ZR,      K_MOUSE1     },
    { WPAD_CLASSIC_BUTTON_UP,      K_UPARROW    },
    { WPAD_CLASSIC_BUTTON_DOWN,    K_DOWNARROW  },
    { WPAD_CLASSIC_BUTTON_LEFT,    K_LEFTARROW  },
    { WPAD_CLASSIC_BUTTON_RIGHT,   K_RIGHTARROW },
};
#define CC_MENU_BTN_COUNT (sizeof(s_cc_menu_buttons) / sizeof(s_cc_menu_buttons[0]))

#define CC_STICK_DEADZONE   0.15f   /* magnitude below which stick is ignored */
#define CC_STICK_SCALE      32767.0f

/* IR aiming constants */
#define IR_CENTER_X       320.0f
#define IR_CENTER_Y       240.0f
#define IR_DEADZONE       40.0f    /* pixels from center before aim registers */
#define IR_SENSITIVITY    0.15f    /* scale factor for IR delta -> mouse delta */
#define IR_MAX_DELTA      25.0f    /* clamp per-frame delta to prevent snaps */

/* Nunchuk joystick: mag is 0.0-1.0, ang is degrees from up clockwise */
#define NUNCHUK_DEADZONE  0.15f    /* magnitude below which stick is ignored */
#define NUNCHUK_SCALE     32767.0f /* scale to Q3 joystick range */

#endif /* WPAD_ENABLED */

/* ---------- Shared state ---------- */

typedef struct {
    qboolean key_held[256];
} input_state_t;

static input_state_t  s_input;
static qboolean       s_home_pressed   = qfalse;
static qboolean       s_in_game        = qfalse;
static float          s_accum_x        = 0.0f;
static float          s_accum_y        = 0.0f;
static float          s_accum_cx       = 0.0f;
static float          s_accum_cy       = 0.0f;
static short          s_old_axis[4];
static qboolean       s_old_ltrig      = qfalse;
static qboolean       s_old_rtrig      = qfalse;
static qboolean       s_bindings_set   = qfalse;

#if WPAD_ENABLED
static qboolean       s_wm_bindings_set  = qfalse;
static qboolean       s_cc_bindings_set  = qfalse;
static qboolean       s_cc_fmt_triggered = qfalse;  /* have we re-triggered format for CC? */
static float          s_ir_last_x        = IR_CENTER_X;
static float          s_ir_last_y        = IR_CENTER_Y;
static qboolean       s_ir_was_valid     = qfalse;
#ifdef WII_DEBUG
static int            s_wpad_diag_count  = 0;
#endif
#endif

/* ---------- Shared helpers ---------- */

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
    /* Zero all axes in the engine so stale values don't persist across state transitions */
    Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_SIDE,    0, 0, NULL);
    Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_FORWARD, 0, 0, NULL);
    Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_YAW,     0, 0, NULL);
    Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_PITCH,   0, 0, NULL);
    s_old_axis[0] = s_old_axis[1] = s_old_axis[2] = s_old_axis[3] = 0;
}

static short GC_FilterAxis(s8 raw, int deadzone)
{
    int v = (int)raw;
    if (v > -deadzone && v < deadzone)
        return 0;
    if (v > 127) v = 127;
    if (v < -127) v = -127;
    return (short)(v * GC_AXIS_SCALE);
}

static void InjectCursorStick(s8 x, s8 y, float sensitivity, int deadzone,
                               float *ax, float *ay)
{
    int ix = (int)x;
    int iy = (int)y;
    if (ix > -deadzone && ix < deadzone) ix = 0;
    if (iy > -deadzone && iy < deadzone) iy = 0;
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

/* ---------- GC controller bindings ---------- */

static void SetGCBindings(void)
{
    if (s_bindings_set)
        return;
    s_bindings_set = qtrue;

    Key_SetBinding(K_JOY1,      "+moveup");     /* A = jump */
    Key_SetBinding(K_JOY2,      "+movedown");   /* B = crouch */
    Key_SetBinding(K_JOY3,      "weapprev");    /* X */
    Key_SetBinding(K_JOY4,      "weapnext");    /* Y */
    Key_SetBinding(K_JOY5,      "+zoom");       /* Z */
    Key_SetBinding(K_JOY6,      "togglemenu");  /* Start */
    Key_SetBinding(K_JOY7,      "+scores");     /* D-up */
    Key_SetBinding(K_JOY8,      "+attack");     /* D-down */
    Key_SetBinding(K_JOY9,      "weapprev");    /* D-left */
    Key_SetBinding(K_JOY10,     "weapnext");    /* D-right */
    Key_SetBinding(K_JOY_LTRIG, "+speed");      /* L = walk */
    Key_SetBinding(K_JOY_RTRIG, "+attack");     /* R = fire */
}

/* ---------- Wiimote+Nunchuk bindings ---------- */

#if WPAD_ENABLED
static void SetWiimoteBindings(void)
{
    if (s_wm_bindings_set)
        return;
    s_wm_bindings_set = qtrue;

    Key_SetBinding(K_JOY1,  "+attack");     /* B trigger = fire */
    Key_SetBinding(K_JOY2,  "+moveup");     /* A = jump */
    Key_SetBinding(K_JOY3,  "+zoom");       /* Nunchuk Z = zoom */
    Key_SetBinding(K_JOY4,  "+movedown");   /* Nunchuk C = crouch */
    Key_SetBinding(K_JOY5,  "togglemenu");  /* + = menu */
    Key_SetBinding(K_JOY6,  "+scores");     /* - = scores */
    Key_SetBinding(K_JOY7,  "weapnext");    /* D-up */
    Key_SetBinding(K_JOY8,  "weapprev");    /* D-down */
    Key_SetBinding(K_JOY9,  "weapprev");    /* D-left */
    Key_SetBinding(K_JOY10, "weapnext");    /* D-right */
    Key_SetBinding(K_JOY11, "+speed");      /* 1 = walk */
}
#endif

/* ---------- Classic Controller bindings ---------- */

#if WPAD_ENABLED
static void SetClassicBindings(void)
{
    if (s_cc_bindings_set)
        return;
    s_cc_bindings_set = qtrue;

    Key_SetBinding(K_JOY1,  "+attack");     /* ZR = fire */
    Key_SetBinding(K_JOY2,  "+moveup");     /* A = jump */
    Key_SetBinding(K_JOY3,  "+movedown");   /* B = crouch */
    Key_SetBinding(K_JOY4,  "+zoom");       /* ZL = zoom */
    Key_SetBinding(K_JOY5,  "weapprev");    /* X */
    Key_SetBinding(K_JOY6,  "weapnext");    /* Y */
    Key_SetBinding(K_JOY7,  "+speed");      /* L = walk */
    Key_SetBinding(K_JOY8,  "+button2");    /* R = use item */
    Key_SetBinding(K_JOY9,  "togglemenu");  /* + */
    Key_SetBinding(K_JOY10, "+scores");     /* - */
    Key_SetBinding(K_JOY11, "weapnext");    /* D-up */
    Key_SetBinding(K_JOY12, "weapprev");    /* D-down */
    Key_SetBinding(K_JOY13, "weapprev");    /* D-left */
    Key_SetBinding(K_JOY14, "weapnext");    /* D-right */
}
#endif

/* ---------- GC controller frame ---------- */

static void GC_Input_Frame(void)
{
    int i;

    PAD_ScanPads();

    u32 held  = PAD_ButtonsHeld(PAD_CHAN0);
    s8  lx    = PAD_StickX(PAD_CHAN0);
    s8  ly    = PAD_StickY(PAD_CHAN0);
    s8  cx    = PAD_SubStickX(PAD_CHAN0);
    s8  cy    = PAD_SubStickY(PAD_CHAN0);
    u8  l_ana = PAD_TriggerL(PAD_CHAN0);
    u8  r_ana = PAD_TriggerR(PAD_CHAN0);

    qboolean in_game = (Key_GetCatcher() == 0) ? qtrue : qfalse;

    if (in_game != s_in_game) {
        ReleaseAllKeys();
        s_in_game = in_game;
    }

    if (!in_game) {
        for (i = 0; i < (int)GC_MENU_BTN_COUNT; i++)
            InjectKey(s_gc_menu_buttons[i].q3key,
                      (held & s_gc_menu_buttons[i].bit) ? qtrue : qfalse);

        InjectKey(K_MOUSE1, r_ana > TRIGGER_THRESHOLD ? qtrue : qfalse);

        InjectCursorStick(lx, ly, MENU_SENSITIVITY_F, STICK_DEADZONE,
                          &s_accum_x, &s_accum_y);
        InjectCursorStick(cx, cy, MENU_SENSITIVITY_F, CSTICK_DEADZONE,
                          &s_accum_cx, &s_accum_cy);
    } else {
        SetGCBindings();

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
        short ax_cx = GC_FilterAxis(cx, CSTICK_DEADZONE);
        short ax_cy = GC_FilterAxis(cy, CSTICK_DEADZONE);


        if (ax_lx != s_old_axis[0]) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_SIDE, ax_lx, 0, NULL);
            s_old_axis[0] = ax_lx;
        }
        short neg_ly = -ax_ly;
        if (neg_ly != s_old_axis[1]) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_FORWARD, neg_ly, 0, NULL);
            s_old_axis[1] = neg_ly;
        }
        /* Send every frame when zero — ensures stale drift never accumulates in engine */
        if (ax_cx != s_old_axis[2] || ax_cx == 0) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_YAW, ax_cx, 0, NULL);
            s_old_axis[2] = ax_cx;
        }
        short neg_cy = -ax_cy;
        if (neg_cy != s_old_axis[3] || neg_cy == 0) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_PITCH, neg_cy, 0, NULL);
            s_old_axis[3] = neg_cy;
        }
    }
}

/* ---------- Wiimote+Nunchuk frame ---------- */

#if WPAD_ENABLED

/* Nunchuk stick -> Q3 joystick axes (mag/ang to X/Y) */
static void WM_NunchukMovement(const struct joystick_t *js)
{
    float mag = js->mag;
    if (mag < NUNCHUK_DEADZONE) {
        /* Center — send zero if we were previously non-zero */
        if (s_old_axis[0] != 0) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_SIDE, 0, 0, NULL);
            s_old_axis[0] = 0;
        }
        if (s_old_axis[1] != 0) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_FORWARD, 0, 0, NULL);
            s_old_axis[1] = 0;
        }
        return;
    }
    if (mag > 1.0f) mag = 1.0f;

    float rad = js->ang * 3.14159265f / 180.0f;
    float sin_a = __builtin_sinf(rad);
    float cos_a = __builtin_cosf(rad);

    short side = (short)(sin_a * mag * NUNCHUK_SCALE);
    short fwd  = (short)(-cos_a * mag * NUNCHUK_SCALE);

    if (side != s_old_axis[0]) {
        Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_SIDE, side, 0, NULL);
        s_old_axis[0] = side;
    }
    if (fwd != s_old_axis[1]) {
        Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_FORWARD, fwd, 0, NULL);
        s_old_axis[1] = fwd;
    }
}

/* IR pointer -> SE_MOUSE deltas (center-relative in-game, position-relative in menus) */
static void WM_IRAiming(const struct ir_t *ir, qboolean in_game)
{
    if (!ir->valid && !ir->smooth_valid) {
        s_ir_was_valid = qfalse;
        return;
    }

    float ix = ir->smooth_valid ? ir->sx : ir->x;
    float iy = ir->smooth_valid ? ir->sy : ir->y;

    if (in_game) {
        float dx = ix - IR_CENTER_X;
        float dy = iy - IR_CENTER_Y;

        if (dx > -IR_DEADZONE && dx < IR_DEADZONE) dx = 0.0f;
        if (dy > -IR_DEADZONE && dy < IR_DEADZONE) dy = 0.0f;

        if (dx == 0.0f && dy == 0.0f)
            return;

        dx *= IR_SENSITIVITY;
        dy *= IR_SENSITIVITY;

        if (dx > IR_MAX_DELTA) dx = IR_MAX_DELTA;
        if (dx < -IR_MAX_DELTA) dx = -IR_MAX_DELTA;
        if (dy > IR_MAX_DELTA) dy = IR_MAX_DELTA;
        if (dy < -IR_MAX_DELTA) dy = -IR_MAX_DELTA;

        int mx = (int)dx;
        int my = (int)dy;
        if (mx != 0 || my != 0)
            Com_QueueEvent(0, SE_MOUSE, mx, my, 0, NULL);
    } else {
        if (s_ir_was_valid) {
            float dx = ix - s_ir_last_x;
            float dy = iy - s_ir_last_y;
            int mx = (int)dx;
            int my = (int)dy;
            if (mx != 0 || my != 0)
                Com_QueueEvent(0, SE_MOUSE, mx, my, 0, NULL);
        }
        s_ir_last_x = ix;
        s_ir_last_y = iy;
        s_ir_was_valid = qtrue;
    }
}

static void CC_Input_Frame(WPADData *data, qboolean in_game)
{
    int i;
    struct classic_ctrl_t *cc = &data->exp.classic;

    if (!in_game) {
        for (i = 0; i < (int)CC_MENU_BTN_COUNT; i++)
            InjectKey(s_cc_menu_buttons[i].q3key,
                      (data->btns_h & s_cc_menu_buttons[i].bit) ? qtrue : qfalse);

        /* Left stick as cursor in menus */
        float mag = cc->ljs.mag;
        if (mag > CC_STICK_DEADZONE) {
            if (mag > 1.0f) mag = 1.0f;
            float rad = cc->ljs.ang * 3.14159265f / 180.0f;
            float fx = __builtin_sinf(rad) * mag * MENU_SENSITIVITY_F;
            float fy = -__builtin_cosf(rad) * mag * MENU_SENSITIVITY_F;
            s_accum_x += fx;
            s_accum_y += fy;
            int ox = (int)s_accum_x;
            int oy = (int)s_accum_y;
            s_accum_x -= (float)ox;
            s_accum_y -= (float)oy;
            if (ox != 0 || oy != 0)
                Com_QueueEvent(0, SE_MOUSE, ox, oy, 0, NULL);
        } else {
            s_accum_x = s_accum_y = 0.0f;
        }
    } else {
        SetClassicBindings();

        for (i = 0; i < (int)CC_BTN_COUNT; i++)
            InjectKey(s_cc_buttons[i].q3key,
                      (data->btns_h & s_cc_buttons[i].bit) ? qtrue : qfalse);

        /* Left stick -> strafe / forward */
        float lmag = cc->ljs.mag;
        if (lmag < CC_STICK_DEADZONE) {
            if (s_old_axis[0] != 0) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_SIDE, 0, 0, NULL);
                s_old_axis[0] = 0;
            }
            if (s_old_axis[1] != 0) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_FORWARD, 0, 0, NULL);
                s_old_axis[1] = 0;
            }
        } else {
            if (lmag > 1.0f) lmag = 1.0f;
            float lrad = cc->ljs.ang * 3.14159265f / 180.0f;
            short side = (short)(__builtin_sinf(lrad) * lmag * CC_STICK_SCALE);
            short fwd  = (short)(-__builtin_cosf(lrad) * lmag * CC_STICK_SCALE);
            if (side != s_old_axis[0]) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_SIDE, side, 0, NULL);
                s_old_axis[0] = side;
            }
            if (fwd != s_old_axis[1]) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_FORWARD, fwd, 0, NULL);
                s_old_axis[1] = fwd;
            }
        }

        /* Right stick -> yaw / pitch */
        float rmag = cc->rjs.mag;
        if (rmag < CC_STICK_DEADZONE) {
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_YAW,   0, 0, NULL);
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_PITCH, 0, 0, NULL);
            s_old_axis[2] = s_old_axis[3] = 0;
        } else {
            if (rmag > 1.0f) rmag = 1.0f;
            float rrad = cc->rjs.ang * 3.14159265f / 180.0f;
            short yaw   = (short)(__builtin_sinf(rrad) * rmag * CC_STICK_SCALE);
            /* stick-up = ang=0 → cos=1 → negate so up = look up (negative pitch) */
            short pitch = (short)(-__builtin_cosf(rrad) * rmag * CC_STICK_SCALE);
            if (yaw != s_old_axis[2] || yaw == 0) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_YAW, yaw, 0, NULL);
                s_old_axis[2] = yaw;
            }
            if (pitch != s_old_axis[3] || pitch == 0) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_PITCH, pitch, 0, NULL);
                s_old_axis[3] = pitch;
            }
        }
    }
}

static void WM_Input_Frame(void)
{
    int i;

    WPAD_ScanPads();
    s_home_pressed = qfalse;

    /* WPAD_Data returns stale data after disconnect; use Probe to detect it */
    u32 probe_type;
    if (WPAD_Probe(WPAD_CHAN_0, &probe_type) != WPAD_ERR_NONE) {
        GC_Input_Frame();
        return;
    }

    WPADData *data = WPAD_Data(WPAD_CHAN_0);
    if (!data || data->err != WPAD_ERR_NONE) {
        GC_Input_Frame();
        return;
    }

    u32 held = data->btns_h;

    if (held & WPAD_BUTTON_HOME)
        s_home_pressed = qtrue;

    PAD_ScanPads();

    qboolean in_game = (Key_GetCatcher() == 0) ? qtrue : qfalse;

    if (in_game != s_in_game) {
        ReleaseAllKeys();
        s_in_game = in_game;
        s_ir_was_valid = qfalse;
    }

    u32 exp_type = WPAD_EXP_NONE;
    WPAD_Probe(WPAD_CHAN_0, &exp_type);
    qboolean has_nunchuk  = (exp_type == WPAD_EXP_NUNCHUK)  ? qtrue : qfalse;
    qboolean has_classic  = (exp_type == WPAD_EXP_CLASSIC)  ? qtrue : qfalse;

#ifdef WII_DEBUG
    if (++s_wpad_diag_count >= 300) {
        s_wpad_diag_count = 0;
        wii_diag("[wpad] exp=%d ir_valid=%d ir_smooth=%d ir=(%.0f,%.0f) btns=0x%08x\n",
                 (int)exp_type, data->ir.valid, data->ir.smooth_valid,
                 data->ir.x, data->ir.y, held);
        if (has_nunchuk) {
            wii_diag("[wpad] nunchuk mag=%.2f ang=%.1f btns=0x%02x\n",
                     data->exp.nunchuk.js.mag,
                     data->exp.nunchuk.js.ang,
                     data->exp.nunchuk.btns);
        }
        if (has_classic) {
            wii_diag("[wpad] classic lmag=%.2f lang=%.1f rmag=%.2f rang=%.1f btns=0x%08x\n",
                     data->exp.classic.ljs.mag, data->exp.classic.ljs.ang,
                     data->exp.classic.rjs.mag, data->exp.classic.rjs.ang,
                     data->btns_h);
        }
    }
#endif

    if (has_classic) {
        /* First time CC is detected: re-apply data format so wiiuse_set_report_type
           picks WM_RPT_BTN_ACC_IR_EXP with EXP now set. Without this, the format
           update from the CC handshake may race with our frame loop and the
           Wiimote keeps sending WM_RPT_BTN_ACC_IR (no expansion data). */
        if (!s_cc_fmt_triggered) {
            s_cc_fmt_triggered = qtrue;
            WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
        }
        CC_Input_Frame(data, in_game);
    } else if (!in_game) {
        /* CC not present — reset so we retrigger format if CC is re-attached */
        s_cc_fmt_triggered = qfalse;

        /* ---- Wiimote menu mode ---- */
        for (i = 0; i < (int)WM_MENU_BTN_COUNT; i++)
            InjectKey(s_wm_menu_buttons[i].q3key,
                      (held & s_wm_menu_buttons[i].bit) ? qtrue : qfalse);

        if (has_nunchuk)
            InjectKey(K_MOUSE1,
                      (held & WPAD_NUNCHUK_BUTTON_Z) ? qtrue : qfalse);

        WM_IRAiming(&data->ir, qfalse);

        /* Nunchuk stick as fallback cursor */
        if (has_nunchuk) {
            struct joystick_t *js = &data->exp.nunchuk.js;
            if (js->mag > NUNCHUK_DEADZONE) {
                float rad = js->ang * 3.14159265f / 180.0f;
                float fx = __builtin_sinf(rad) * js->mag * MENU_SENSITIVITY_F;
                float fy = -__builtin_cosf(rad) * js->mag * MENU_SENSITIVITY_F;
                s_accum_x += fx;
                s_accum_y += fy;
                int ox = (int)s_accum_x;
                int oy = (int)s_accum_y;
                s_accum_x -= (float)ox;
                s_accum_y -= (float)oy;
                if (ox != 0 || oy != 0)
                    Com_QueueEvent(0, SE_MOUSE, ox, oy, 0, NULL);
            } else {
                s_accum_x = s_accum_y = 0.0f;
            }
        }
    } else {
        s_cc_fmt_triggered = qfalse;

        /* ---- Wiimote game mode ---- */
        SetWiimoteBindings();

        for (i = 0; i < (int)WM_BTN_COUNT; i++)
            InjectKey(s_wm_buttons[i].q3key,
                      (held & s_wm_buttons[i].bit) ? qtrue : qfalse);

        WM_IRAiming(&data->ir, qtrue);

        if (has_nunchuk) {
            WM_NunchukMovement(&data->exp.nunchuk.js);
        }
    }

}

#endif /* WPAD_ENABLED */

/* ---------- USB keyboard ---------- */

static qboolean s_kb_inited  = qfalse;
static qboolean s_mouse_inited = qfalse;

/* Translate libogc keyboard symbol (KS_*) to Q3 keycode.
   Returns 0 for unmapped keys. */
static int KB_SymToQ3Key(u16 sym)
{
    /* ASCII printable range — Q3 wants lowercase */
    if (sym >= KS_space && sym <= KS_asciitilde) {
        int ch = (int)sym;
        if (ch >= 'A' && ch <= 'Z')
            ch = ch - 'A' + 'a';
        return ch;
    }

    switch (sym) {
    case KS_BackSpace:  return K_BACKSPACE;
    case KS_Tab:        return K_TAB;
    case KS_Return:     return K_ENTER;
    case KS_Escape:     return K_ESCAPE;
    case KS_Delete:     return K_DEL;

    /* Arrow / navigation */
    case KS_Up:         return K_UPARROW;
    case KS_Down:       return K_DOWNARROW;
    case KS_Left:       return K_LEFTARROW;
    case KS_Right:      return K_RIGHTARROW;
    case KS_Home:       return K_HOME;
    case KS_End:        return K_END;
    case KS_Prior:      return K_PGUP;
    case KS_Next:       return K_PGDN;
    case KS_Insert:     return K_INS;
    case KS_Pause:      return K_PAUSE;

    /* Modifiers */
    case KS_Shift_L:
    case KS_Shift_R:    return K_SHIFT;
    case KS_Control_L:
    case KS_Control_R:  return K_CTRL;
    case KS_Alt_L:
    case KS_Alt_R:      return K_ALT;
    case KS_Caps_Lock:  return K_CAPSLOCK;

    /* Function keys */
    case KS_f1:  case KS_F1:  return K_F1;
    case KS_f2:  case KS_F2:  return K_F2;
    case KS_f3:  case KS_F3:  return K_F3;
    case KS_f4:  case KS_F4:  return K_F4;
    case KS_f5:  case KS_F5:  return K_F5;
    case KS_f6:  case KS_F6:  return K_F6;
    case KS_f7:  case KS_F7:  return K_F7;
    case KS_f8:  case KS_F8:  return K_F8;
    case KS_f9:  case KS_F9:  return K_F9;
    case KS_f10: case KS_F10: return K_F10;
    case KS_f11: case KS_F11: return K_F11;
    case KS_f12: case KS_F12: return K_F12;

    /* Keypad */
    case KS_KP_Enter:    return K_KP_ENTER;
    case KS_KP_Add:      return K_KP_PLUS;
    case KS_KP_Subtract: return K_KP_MINUS;
    case KS_KP_Multiply: return K_KP_STAR;
    case KS_KP_Divide:   return K_KP_SLASH;
    case KS_KP_Equal:    return K_KP_EQUALS;
    case KS_KP_Delete:   return K_KP_DEL;
    case KS_KP_Insert:   return K_KP_INS;
    case KS_KP_Home:     return K_KP_HOME;
    case KS_KP_End:      return K_KP_END;
    case KS_KP_Up:       return K_KP_UPARROW;
    case KS_KP_Down:     return K_KP_DOWNARROW;
    case KS_KP_Left:     return K_KP_LEFTARROW;
    case KS_KP_Right:    return K_KP_RIGHTARROW;
    case KS_KP_Prior:    return K_KP_PGUP;
    case KS_KP_Next:     return K_KP_PGDN;
    case KS_KP_Begin:    return K_KP_5;
    case KS_Num_Lock:    return K_KP_NUMLOCK;
    case KS_KP_0:        return K_KP_INS;
    case KS_KP_1:        return K_KP_END;
    case KS_KP_2:        return K_KP_DOWNARROW;
    case KS_KP_3:        return K_KP_PGDN;
    case KS_KP_4:        return K_KP_LEFTARROW;
    case KS_KP_5:        return K_KP_5;
    case KS_KP_6:        return K_KP_RIGHTARROW;
    case KS_KP_7:        return K_KP_HOME;
    case KS_KP_8:        return K_KP_UPARROW;
    case KS_KP_9:        return K_KP_PGUP;
    case KS_KP_Decimal:  return K_KP_DEL;

    default: return 0;
    }
}

/* Drain pending USB keyboard events and inject into Q3 */
static void USB_Keyboard_Frame(void)
{
    keyboard_event evt;
    int max_events = 32; /* safety cap per frame */

    while (max_events-- > 0 && KEYBOARD_GetEvent(&evt) > 0) {
        if (evt.type == KEYBOARD_PRESSED || evt.type == KEYBOARD_RELEASED) {
            qboolean down = (evt.type == KEYBOARD_PRESSED) ? qtrue : qfalse;

            /* Console toggle: ~ or ` */
            if (down && (evt.symbol == KS_grave || evt.symbol == KS_asciitilde)) {
                Com_QueueEvent(0, SE_KEY, K_CONSOLE, qtrue, 0, NULL);
                Com_QueueEvent(0, SE_KEY, K_CONSOLE, qfalse, 0, NULL);
                continue;
            }

            int q3key = KB_SymToQ3Key(evt.symbol);
            if (q3key == 0)
                continue;

            Com_QueueEvent(0, SE_KEY, q3key, down, 0, NULL);

            /* On key press, also send SE_CHAR for text input */
            if (down) {
                if (q3key == K_BACKSPACE) {
                    Com_QueueEvent(0, SE_CHAR, 8, 0, 0, NULL); /* Ctrl-H */
                } else if (evt.symbol >= KS_space && evt.symbol <= KS_asciitilde) {
                    /* Send the original symbol (preserving case/shift) */
                    Com_QueueEvent(0, SE_CHAR, (int)evt.symbol, 0, 0, NULL);
                } else if (q3key == K_ENTER || q3key == K_KP_ENTER) {
                    Com_QueueEvent(0, SE_CHAR, '\r', 0, 0, NULL);
                } else if (q3key == K_TAB) {
                    Com_QueueEvent(0, SE_CHAR, '\t', 0, 0, NULL);
                }
            }
        }
    }
}

/* ---------- USB mouse ---------- */

static u8 s_mouse_old_buttons = 0;

/* Drain pending USB mouse events and inject into Q3 */
static void USB_Mouse_Frame(void)
{
    mouse_event evt;
    int dx = 0, dy = 0;
    u8 cur_buttons = s_mouse_old_buttons;
    int max_events = 64;

    /* Accumulate all pending mouse events this frame */
    while (max_events-- > 0 && MOUSE_GetEvent(&evt) > 0) {
        dx += evt.rx;
        dy += evt.ry;
        cur_buttons = evt.button;

        /* Scroll wheel */
        if (evt.rz > 0)
            for (int i = 0; i < evt.rz; i++) {
                Com_QueueEvent(0, SE_KEY, K_MWHEELUP, qtrue, 0, NULL);
                Com_QueueEvent(0, SE_KEY, K_MWHEELUP, qfalse, 0, NULL);
            }
        else if (evt.rz < 0)
            for (int i = 0; i < -evt.rz; i++) {
                Com_QueueEvent(0, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL);
                Com_QueueEvent(0, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL);
            }
    }

    /* Movement */
    if (dx != 0 || dy != 0)
        Com_QueueEvent(0, SE_MOUSE, dx, dy, 0, NULL);

    /* Buttons: bit 0 = left, bit 1 = right, bit 2 = middle */
    static const int mouse_keys[] = { K_MOUSE1, K_MOUSE2, K_MOUSE3 };
    int i;
    for (i = 0; i < 3; i++) {
        qboolean was = (s_mouse_old_buttons & (1 << i)) ? qtrue : qfalse;
        qboolean now = (cur_buttons & (1 << i)) ? qtrue : qfalse;
        if (was != now)
            Com_QueueEvent(0, SE_KEY, mouse_keys[i], now, 0, NULL);
    }
    s_mouse_old_buttons = cur_buttons;
}

/* ---------- Public API ---------- */

void Wii_Input_Init(void)
{
    memset(&s_input, 0, sizeof(s_input));
    s_home_pressed  = qfalse;
    s_in_game       = qfalse;
    s_bindings_set  = qfalse;
    s_accum_x = s_accum_y = 0.0f;
    s_accum_cx = s_accum_cy = 0.0f;
    s_old_ltrig = s_old_rtrig = qfalse;
    memset(s_old_axis, 0, sizeof(s_old_axis));

    PAD_Init();

#if WPAD_ENABLED
    s_wm_bindings_set  = qfalse;
    s_cc_bindings_set  = qfalse;
    s_cc_fmt_triggered = qfalse;
    s_ir_last_x = IR_CENTER_X;
    s_ir_last_y = IR_CENTER_Y;
    s_ir_was_valid = qfalse;

    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_0, 640, 480);
    WPAD_SetIdleTimeout(300); /* 5 minutes before auto-disconnect */

#ifdef WII_DEBUG
    printf("[input] Wiimote+Nunchuk initialised (IR aiming)\n");
#endif
#else
#ifdef WII_DEBUG
    printf("[input] GameCube PAD initialised (dual-stick)\n");
#endif
#endif

    /* USB keyboard */
    if (KEYBOARD_Init(NULL) >= 0) {
        s_kb_inited = qtrue;
#ifdef WII_DEBUG
        printf("[input] USB keyboard initialised\n");
#endif
    }

    /* USB mouse */
    if (MOUSE_Init() >= 0) {
        s_mouse_inited = qtrue;
        s_mouse_old_buttons = 0;
#ifdef WII_DEBUG
        printf("[input] USB mouse initialised\n");
#endif
    }

}

void Wii_Input_SetCvars(void)
{
    Cvar_Set("in_joystick",          "1");
    Cvar_Set("in_joystickUseAnalog", "1");
    Cvar_Set("j_side_axis",    "0");
    Cvar_Set("j_forward_axis", "1");
    Cvar_Set("j_pitch_axis",   "3");
    Cvar_Set("j_yaw_axis",     "4");
    Cvar_Set("j_side",    "0.25");
    Cvar_Set("j_forward", "-0.25");
    Cvar_Set("j_pitch",   "0.015");
    Cvar_Set("j_yaw",    "-0.015");

    Cvar_Set("vm_ui",    "1");
    Cvar_Set("vm_cgame", "1");
    Cvar_Set("vm_game",  "1");

}

void Wii_Input_Frame(void)
{
#if WPAD_ENABLED
    WM_Input_Frame();
#else
    s_home_pressed = qfalse;
    GC_Input_Frame();
#endif

    /* USB devices layer on top of controller input */
    if (s_kb_inited)
        USB_Keyboard_Frame();
    if (s_mouse_inited)
        USB_Mouse_Frame();
}

qboolean Wii_Input_HomePressed(void)
{
    return s_home_pressed;
}
