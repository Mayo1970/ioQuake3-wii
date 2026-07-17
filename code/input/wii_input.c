/* Unified controller input: GC pad + optional Wiimote/Nunchuk/Classic,
   plus USB keyboard and mouse. */

#ifndef WPAD_ENABLED
#define WPAD_ENABLED  0
#endif

#include <gccore.h>
#include <ogc/pad.h>
#include <ogc/usbmouse.h>
#include <wiikeyboard/keyboard.h>
#if WPAD_ENABLED
#include <wiiuse/wpad.h>
#include <wiidrc/wiidrc.h>   /* Wii U GamePad (DRC) — vWii only */
#endif
#include <string.h>
#include <stdio.h>

#include "wii_input.h"
#include "wii_usb_hid.h"
#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "keycodes.h"

extern int   Key_GetCatcher(void);
extern void  Key_SetBinding(int keynum, const char *binding);
extern char *Key_GetBinding(int keynum);
extern char *Key_KeynumToString(int keynum);
extern int   wii_keynumstr_raw;   /* cl_keys.c — 1 = canonical "JOYn" names */

#define STICK_DEADZONE       20
#define CSTICK_DEADZONE      50
#define MENU_SENSITIVITY_F   6.0f   /* 2x: input is now polled once/frame (was 2x; see wii_main.c) */
#define TRIGGER_THRESHOLD    100

#define AXIS_SIDE     0
#define AXIS_FORWARD  1
#define AXIS_YAW      4
#define AXIS_PITCH    3

#define GC_AXIS_SCALE  258

typedef struct { u32 bit; int q3key; } btn_map_t;

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

/* Analog triggers folded into the menu-path button mask as synthetic bits so
   the raw bind-capture layer treats them like any other button (PAD_BUTTON_*
   occupy the low u16; these don't collide). */
#define GC_SYNTH_LTRIG  0x40000000u
#define GC_SYNTH_RTRIG  0x80000000u

static const btn_map_t s_gc_menu_buttons[] = {
    { PAD_BUTTON_A,      K_ENTER      },
    { PAD_BUTTON_B,      K_ESCAPE     },
    { PAD_BUTTON_X,      K_MOUSE1     },
    { PAD_BUTTON_Y,      K_CONSOLE    },
    { PAD_BUTTON_UP,     K_UPARROW    },
    { PAD_BUTTON_DOWN,   K_DOWNARROW  },
    { PAD_BUTTON_LEFT,   K_LEFTARROW  },
    { PAD_BUTTON_RIGHT,  K_RIGHTARROW },
};
#define GC_MENU_BTN_COUNT (sizeof(s_gc_menu_buttons) / sizeof(s_gc_menu_buttons[0]))

#if WPAD_ENABLED

static const btn_map_t s_wm_buttons[] = {
    { WPAD_BUTTON_B,              K_JOY1  },
    { WPAD_BUTTON_A,              K_JOY2  },
    { WPAD_NUNCHUK_BUTTON_Z,      K_JOY3  },
    { WPAD_NUNCHUK_BUTTON_C,      K_JOY4  },
    { WPAD_BUTTON_PLUS,           K_JOY5  },
    { WPAD_BUTTON_MINUS,          K_JOY6  },
    { WPAD_BUTTON_UP,             K_JOY7  },
    { WPAD_BUTTON_DOWN,           K_JOY8  },
    { WPAD_BUTTON_LEFT,           K_JOY9  },
    { WPAD_BUTTON_RIGHT,          K_JOY10 },
    { WPAD_BUTTON_1,              K_JOY11 },
};
#define WM_BTN_COUNT (sizeof(s_wm_buttons) / sizeof(s_wm_buttons[0]))

static const btn_map_t s_wm_menu_buttons[] = {
    { WPAD_BUTTON_A,              K_ENTER      },
    { WPAD_BUTTON_B,              K_ESCAPE     },
    { WPAD_BUTTON_1,              K_MOUSE1     },
    { WPAD_BUTTON_UP,             K_UPARROW    },
    { WPAD_BUTTON_DOWN,           K_DOWNARROW  },
    { WPAD_BUTTON_LEFT,           K_LEFTARROW  },
    { WPAD_BUTTON_RIGHT,          K_RIGHTARROW },
};
#define WM_MENU_BTN_COUNT (sizeof(s_wm_menu_buttons) / sizeof(s_wm_menu_buttons[0]))

static const btn_map_t s_cc_buttons[] = {
    { WPAD_CLASSIC_BUTTON_ZR,      K_JOY1  },
    { WPAD_CLASSIC_BUTTON_A,       K_JOY2  },
    { WPAD_CLASSIC_BUTTON_B,       K_JOY3  },
    { WPAD_CLASSIC_BUTTON_ZL,      K_JOY4  },
    { WPAD_CLASSIC_BUTTON_X,       K_JOY5  },
    { WPAD_CLASSIC_BUTTON_Y,       K_JOY6  },
    { WPAD_CLASSIC_BUTTON_FULL_L,  K_JOY7  },
    { WPAD_CLASSIC_BUTTON_FULL_R,  K_JOY8  },
    { WPAD_CLASSIC_BUTTON_PLUS,    K_JOY9  },
    { WPAD_CLASSIC_BUTTON_MINUS,   K_JOY10 },
    { WPAD_CLASSIC_BUTTON_UP,      K_JOY11 },
    { WPAD_CLASSIC_BUTTON_DOWN,    K_JOY12 },
    { WPAD_CLASSIC_BUTTON_LEFT,    K_JOY13 },
    { WPAD_CLASSIC_BUTTON_RIGHT,   K_JOY14 },
};
#define CC_BTN_COUNT (sizeof(s_cc_buttons) / sizeof(s_cc_buttons[0]))

static const btn_map_t s_cc_menu_buttons[] = {
    { WPAD_CLASSIC_BUTTON_A,       K_ENTER      },
    { WPAD_CLASSIC_BUTTON_B,       K_ESCAPE     },
    { WPAD_CLASSIC_BUTTON_ZR,      K_MOUSE1     },
    { WPAD_CLASSIC_BUTTON_UP,      K_UPARROW    },
    { WPAD_CLASSIC_BUTTON_DOWN,    K_DOWNARROW  },
    { WPAD_CLASSIC_BUTTON_LEFT,    K_LEFTARROW  },
    { WPAD_CLASSIC_BUTTON_RIGHT,   K_RIGHTARROW },
};
#define CC_MENU_BTN_COUNT (sizeof(s_cc_menu_buttons) / sizeof(s_cc_menu_buttons[0]))

#define CC_STICK_DEADZONE   0.15f   /* magnitude below which stick is ignored */
#define CC_STICK_SCALE      32767.0f

/* Wii U GamePad (DRC) — layout mirrors Classic Controller for consistent K_JOY assignments. */
static const btn_map_t s_drc_buttons[] = {
    { WIIDRC_BUTTON_ZR,     K_JOY1  },
    { WIIDRC_BUTTON_A,      K_JOY2  },
    { WIIDRC_BUTTON_B,      K_JOY3  },
    { WIIDRC_BUTTON_ZL,     K_JOY4  },
    { WIIDRC_BUTTON_X,      K_JOY5  },
    { WIIDRC_BUTTON_Y,      K_JOY6  },
    { WIIDRC_BUTTON_L,      K_JOY7  },
    { WIIDRC_BUTTON_R,      K_JOY8  },
    { WIIDRC_BUTTON_PLUS,   K_JOY9  },
    { WIIDRC_BUTTON_MINUS,  K_JOY10 },
    { WIIDRC_BUTTON_UP,     K_JOY11 },
    { WIIDRC_BUTTON_DOWN,   K_JOY12 },
    { WIIDRC_BUTTON_LEFT,   K_JOY13 },
    { WIIDRC_BUTTON_RIGHT,  K_JOY14 },
};
#define DRC_BTN_COUNT (sizeof(s_drc_buttons) / sizeof(s_drc_buttons[0]))

static const btn_map_t s_drc_menu_buttons[] = {
    { WIIDRC_BUTTON_A,      K_ENTER      },
    { WIIDRC_BUTTON_B,      K_ESCAPE     },
    { WIIDRC_BUTTON_ZR,     K_MOUSE1     },
    { WIIDRC_BUTTON_UP,     K_UPARROW    },
    { WIIDRC_BUTTON_DOWN,   K_DOWNARROW  },
    { WIIDRC_BUTTON_LEFT,   K_LEFTARROW  },
    { WIIDRC_BUTTON_RIGHT,  K_RIGHTARROW },
};
#define DRC_MENU_BTN_COUNT (sizeof(s_drc_menu_buttons) / sizeof(s_drc_menu_buttons[0]))

/* DRC axes are -128..+127 after calibration; normalise against this half-range. */
#define DRC_STICK_RANGE     128.0f
#define DRC_STICK_DEADZONE  0.15f   /* fraction of full range */
#define DRC_STICK_SCALE     32767.0f

#define IR_CENTER_X       320.0f
#define IR_CENTER_Y       240.0f

static cvar_t *ir_deadzone;
static cvar_t *ir_sensitivity;
static cvar_t *ir_maxDelta;
cvar_t *ir_yawRange;
cvar_t *ir_pitchRange;

#define NUNCHUK_DEADZONE  0.15f
#define NUNCHUK_SCALE     32767.0f

#endif /* WPAD_ENABLED */

typedef struct {
    qboolean key_held[256];
} input_state_t;

float wii_ir_aim_x = 0.0f;
float wii_ir_aim_y = 0.0f;

#define CTRL_TYPE_NONE     0
#define CTRL_TYPE_GC       1
#define CTRL_TYPE_WIIMOTE  2
#define CTRL_TYPE_CLASSIC  3
#define CTRL_TYPE_DRC      4   /* Wii U GamePad (tablet), vWii only */
#define CTRL_TYPE_USB      5   /* wired USB HID pad (Xbox One/PS4/PS3) */

static input_state_t  s_input;
static qboolean       s_home_pressed   = qfalse;
static qboolean       s_in_game        = qfalse;
static float          s_accum_x        = 0.0f;
static float          s_accum_y        = 0.0f;
static float          s_accum_cx       = 0.0f;
static float          s_accum_cy       = 0.0f;
static short          s_old_axis[4];
/* Tracked outside key_held[] so a held Start surviving ReleaseAllKeys()'s
 * state wipe doesn't fake a menu-toggle edge. Found this the hard way. */
static qboolean       s_wm_plus_prev   = qfalse;
static qboolean       s_drc_plus_prev  = qfalse;
static qboolean       s_usb_start_prev = qfalse;
static int            s_active_ctrl_type = -1;

/* Raw bind-capture layer (menus only). The menu maps deliberately hide the
 * K_JOYn codes from the UI so A/B/D-pad can navigate — which also made every
 * pad button impossible to bind from the retail Controls menu (its grabber
 * binds any keynum it receives, but never saw a JOY code). Holding a
 * per-controller modifier (Minus; GC: Z, USB: Back/Share) while a menu is open
 * switches emission to the in-game map so the grabber captures real JOY codes;
 * tapping the modifier alone emits its own JOY code on release so the modifier
 * itself stays bindable. All modifier codes sit outside K_JOY1–K_JOY4 (the
 * codes retail menus treat as ENTER), so a stray tap in normal navigation is
 * inert. See MenuRawLayer(). */
static qboolean       s_menu_raw      = qfalse; /* modifier held in a menu */
static qboolean       s_menu_mod_used = qfalse; /* other button seen during hold */
static u32            s_menu_suppress = 0;      /* held-over bits: no menu key
                                                   re-assert until released */

#if WPAD_ENABLED
static qboolean       s_cc_fmt_triggered = qfalse;
static float          s_ir_last_x        = IR_CENTER_X;
static float          s_ir_last_y        = IR_CENTER_Y;
static qboolean       s_ir_was_valid     = qfalse;
#ifdef WII_DEBUG
static int            s_wpad_diag_count  = 0;
#endif
#endif


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
    wii_ir_aim_x = wii_ir_aim_y = 0.0f;
    /* Zero engine-side axes to prevent stale values on state transitions. */
    Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_SIDE,    0, 0, NULL);
    Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_FORWARD, 0, 0, NULL);
    Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_YAW,     0, 0, NULL);
    Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_PITCH,   0, 0, NULL);
    s_old_axis[0] = s_old_axis[1] = s_old_axis[2] = s_old_axis[3] = 0;
    /* Any state wipe (menu open/close, hotswap) also exits the raw
       bind-capture layer cleanly: no pending tap, no stale suppression.
       MenuRawLayer() re-establishes its state after calling this. */
    s_menu_raw      = qfalse;
    s_menu_mod_used = qtrue;
    s_menu_suppress = 0;
}

/* Raw bind-capture layer — call at the top of a menu branch (never in-game).
   Returns qtrue while the modifier is held: the in-game JOY codes (minus the
   modifier's own) have been emitted and the caller must skip ALL menu-key
   emission this frame. Returns qfalse otherwise, with *held stripped of
   buttons still physically down since the raw layer exited — they must not
   re-assert their menu keys (K_ENTER re-activating the bind row was a real
   failure mode) until released. */
static qboolean MenuRawLayer(const btn_map_t *map, int count, u32 *held,
                             u32 mod_bit, int mod_key)
{
    qboolean raw = (*held & mod_bit) ? qtrue : qfalse;
    int i;

    if (raw != s_menu_raw) {
        if (raw) {
            ReleaseAllKeys();          /* drop held menu keys before the flip */
            s_menu_raw      = qtrue;   /* set after the wipe's layer reset */
            s_menu_mod_used = qfalse;  /* fresh hold — a tap is possible */
        } else {
            qboolean tap = !s_menu_mod_used;
            ReleaseAllKeys();          /* drop held JOY keys; resets layer state */
            s_menu_suppress = *held & ~mod_bit;
            if (tap) {
                /* Bind-grabbers act on key-down; the immediate up keeps
                   key_held[] honest and the retail menus ignore up events. */
                InjectKey(mod_key, qtrue);
                InjectKey(mod_key, qfalse);
            }
        }
    }

    if (!raw) {
        s_menu_suppress &= *held;      /* released buttons leave the mask */
        *held &= ~s_menu_suppress;
        return qfalse;
    }

    for (i = 0; i < count; i++) {
        if (map[i].bit == mod_bit)
            continue;
        if (*held & map[i].bit)
            s_menu_mod_used = qtrue;
        InjectKey(map[i].q3key, (*held & map[i].bit) ? qtrue : qfalse);
    }
    return qtrue;
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

#define CTRL_KEY_FIRST  K_JOY1
#define CTRL_KEY_LAST   K_JOY16   /* highest index used by the USB pad's analog triggers */

#define K_JOY_USB_LTRIG K_JOY15
#define K_JOY_USB_RTRIG K_JOY16

static const char *CtrlTypeCfgName(int type)
{
    switch (type) {
    case CTRL_TYPE_GC:      return "wii_binds_gc.cfg";
    case CTRL_TYPE_WIIMOTE: return "wii_binds_wm.cfg";
    case CTRL_TYPE_CLASSIC: return "wii_binds_cc.cfg";
    case CTRL_TYPE_DRC:     return "wii_binds_drc.cfg";
    case CTRL_TYPE_USB:     return "wii_binds_usb.cfg";
    default:                return NULL;
    }
}

static void SaveControllerBindings(int type)
{
    const char *cfgname = CtrlTypeCfgName(type);
    fileHandle_t f;
    int k;

    if (!cfgname)
        return;

    f = FS_FOpenFileWrite_HomeConfig(cfgname);
    if (!f) {
        Com_Printf("wii_input: couldn't write %s\n", cfgname);
        return;
    }

    /* Canonical "JOYn" names, same as Key_WriteBindings — the friendly labels
       ("A", "ZR", "D-Up") don't round-trip through Key_StringToKeynum, so a
       cfg written with them loses every multi-char bind on exec (and binds
       keyboard letters for the single-char ones). */
    wii_keynumstr_raw = 1;
    for (k = CTRL_KEY_FIRST; k <= CTRL_KEY_LAST; k++) {
        char *bind = Key_GetBinding(k);
        if (bind && bind[0])
            FS_Printf(f, "bind %s \"%s\"\n", Key_KeynumToString(k), bind);
    }
    wii_keynumstr_raw = 0;

    FS_FCloseFile(f);
}

static void ApplyBind(int keynum, const char *binding, qboolean force)
{
    if (force) {
        Key_SetBinding(keynum, binding);
    } else {
        char *existing = Key_GetBinding(keynum);
        if (!existing || !existing[0])
            Key_SetBinding(keynum, binding);
    }
}

static qboolean CtrlCfgExists(int type)
{
    const char *cfgname = CtrlTypeCfgName(type);
    fileHandle_t f;
    long len;

    if (!cfgname)
        return qfalse;

    len = FS_BaseDir_FOpenFileRead(cfgname, &f);
    if (len > 0) {
        FS_FCloseFile(f);
        return qtrue;
    }
    return qfalse;
}

static qboolean SetActiveControllerType(int type)
{
    if (s_active_ctrl_type != -1 && s_active_ctrl_type != type) {
        SaveControllerBindings(s_active_ctrl_type);
        /* Release keys only the outgoing controller can release (e.g. its
           trigger keys) so nothing stays held across the hotswap. */
        ReleaseAllKeys();
        s_wm_plus_prev = s_drc_plus_prev = s_usb_start_prev = qfalse;
    }

    Cvar_SetValue("wii_lastControllerType", (float)type);
    s_active_ctrl_type = type;

    if (CtrlCfgExists(type)) {
        const char *cfgname = CtrlTypeCfgName(type);
        Cbuf_AddText(va("exec %s\n", cfgname));
        return qfalse; /* don't overwrite with defaults */
    }

    return qtrue; /* no saved cfg — caller should apply defaults */
}

static void SetGCBindings(void)
{
    if (s_active_ctrl_type == CTRL_TYPE_GC)
        return;

    qboolean force = SetActiveControllerType(CTRL_TYPE_GC);

    ApplyBind(K_JOY1,      "+moveup",     force); /* A = jump */
    ApplyBind(K_JOY2,      "+movedown",   force); /* B = crouch */
    ApplyBind(K_JOY3,      "weapnext",    force); /* X */
    ApplyBind(K_JOY4,      "weapprev",    force); /* Y */
    ApplyBind(K_JOY5,      "+button2",    force); /* Z = use item */
    ApplyBind(K_JOY6,      "togglemenu",  force); /* Start */
    ApplyBind(K_JOY7,      "+scores",     force); /* D-up */
    /* K_JOY8 = D-Down: unbound */
    ApplyBind(K_JOY9,      "+moveleft",   force); /* D-left = strafe left */
    ApplyBind(K_JOY10,     "+moveright",  force); /* D-right = strafe right */
    ApplyBind(K_JOY_LTRIG, "+zoom",       force); /* L = zoom */
    ApplyBind(K_JOY_RTRIG, "+attack",     force); /* R = fire */
    /* Clear default.cfg MOUSE1 so the controls screen shows the JOY key name instead. */
    Key_SetBinding(K_MOUSE1, "");
}

#if WPAD_ENABLED
static void SetWiimoteBindings(void)
{
    if (s_active_ctrl_type == CTRL_TYPE_WIIMOTE)
        return;

    qboolean force = SetActiveControllerType(CTRL_TYPE_WIIMOTE);

    ApplyBind(K_JOY1,  "+attack",    force); /* B trigger = fire */
    ApplyBind(K_JOY2,  "+moveup",    force); /* A = jump */
    ApplyBind(K_JOY3,  "+zoom",      force); /* Nunchuk Z = zoom */
    ApplyBind(K_JOY4,  "+movedown",  force); /* Nunchuk C = crouch */
    ApplyBind(K_JOY5,  "togglemenu", force); /* + = menu */
    ApplyBind(K_JOY6,  "+scores",    force); /* - = scores */
    /* K_JOY7 = D-Up: unbound */
    /* K_JOY8 = D-Down: unbound */
    ApplyBind(K_JOY9,  "weapprev",   force); /* D-left */
    ApplyBind(K_JOY10, "weapnext",   force); /* D-right */
    ApplyBind(K_JOY11, "+speed",     force); /* 1 = walk */
    Key_SetBinding(K_MOUSE1, "");
}
#endif

#if WPAD_ENABLED
static void SetClassicBindings(void)
{
    if (s_active_ctrl_type == CTRL_TYPE_CLASSIC)
        return;

    qboolean force = SetActiveControllerType(CTRL_TYPE_CLASSIC);

    ApplyBind(K_JOY1,  "+attack",    force); /* ZR = fire */
    ApplyBind(K_JOY2,  "+moveup",    force); /* A = jump */
    ApplyBind(K_JOY3,  "+movedown",  force); /* B = crouch */
    ApplyBind(K_JOY4,  "+zoom",      force); /* ZL = zoom */
    ApplyBind(K_JOY5,  "weapnext",   force); /* X */
    ApplyBind(K_JOY6,  "weapprev",   force); /* Y */
    ApplyBind(K_JOY7,  "+speed",     force); /* L = walk */
    ApplyBind(K_JOY8,  "+button2",   force); /* R = use item */
    ApplyBind(K_JOY9,  "togglemenu", force); /* + */
    ApplyBind(K_JOY10, "+scores",    force); /* - */
    ApplyBind(K_JOY11, "+forward",   force); /* D-up */
    ApplyBind(K_JOY12, "+back",      force); /* D-down */
    ApplyBind(K_JOY13, "+moveleft",  force); /* D-left */
    ApplyBind(K_JOY14, "+moveright", force); /* D-right */
    Key_SetBinding(K_MOUSE1, "");
}
#endif

#if WPAD_ENABLED
static void SetDRCBindings(void)
{
    if (s_active_ctrl_type == CTRL_TYPE_DRC)
        return;

    qboolean force = SetActiveControllerType(CTRL_TYPE_DRC);

    ApplyBind(K_JOY1,  "+attack",    force); /* ZR = fire */
    ApplyBind(K_JOY2,  "+moveup",    force); /* A = jump */
    ApplyBind(K_JOY3,  "+movedown",  force); /* B = crouch */
    ApplyBind(K_JOY4,  "+zoom",      force); /* ZL = zoom */
    ApplyBind(K_JOY5,  "weapprev",   force); /* X */
    ApplyBind(K_JOY6,  "weapnext",   force); /* Y */
    ApplyBind(K_JOY7,  "+speed",     force); /* L = walk */
    ApplyBind(K_JOY8,  "+button2",   force); /* R = use item */
    ApplyBind(K_JOY9,  "togglemenu", force); /* + */
    ApplyBind(K_JOY10, "+scores",    force); /* - */
    ApplyBind(K_JOY11, "weapnext",   force); /* D-up */
    ApplyBind(K_JOY12, "weapprev",   force); /* D-down */
    ApplyBind(K_JOY13, "weapprev",   force); /* D-left */
    ApplyBind(K_JOY14, "weapnext",   force); /* D-right */
    Key_SetBinding(K_MOUSE1, "");
}
#endif

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

    SetGCBindings();

    if (!in_game) {
        u32 menu_held = held;
        if (l_ana > TRIGGER_THRESHOLD) menu_held |= GC_SYNTH_LTRIG;
        if (r_ana > TRIGGER_THRESHOLD) menu_held |= GC_SYNTH_RTRIG;

        if (MenuRawLayer(s_gc_buttons, (int)GC_BTN_COUNT, &menu_held,
                         PAD_TRIGGER_Z, K_JOY5)) {
            if (menu_held & (GC_SYNTH_LTRIG | GC_SYNTH_RTRIG))
                s_menu_mod_used = qtrue;
            InjectKey(K_JOY_LTRIG, (menu_held & GC_SYNTH_LTRIG) ? qtrue : qfalse);
            InjectKey(K_JOY_RTRIG, (menu_held & GC_SYNTH_RTRIG) ? qtrue : qfalse);
        } else {
            for (i = 0; i < (int)GC_MENU_BTN_COUNT; i++)
                InjectKey(s_gc_menu_buttons[i].q3key,
                          (menu_held & s_gc_menu_buttons[i].bit) ? qtrue : qfalse);

            InjectKey(K_MOUSE1, (menu_held & GC_SYNTH_RTRIG) ? qtrue : qfalse);
        }

        InjectCursorStick(lx, ly, MENU_SENSITIVITY_F, STICK_DEADZONE,
                          &s_accum_x, &s_accum_y);
        InjectCursorStick(cx, cy, MENU_SENSITIVITY_F, CSTICK_DEADZONE,
                          &s_accum_cx, &s_accum_cy);
    } else {
        for (i = 0; i < (int)GC_BTN_COUNT; i++)
            InjectKey(s_gc_buttons[i].q3key,
                      (held & s_gc_buttons[i].bit) ? qtrue : qfalse);

        /* Through InjectKey like every other button so key_held[] tracks the
           triggers: ReleaseAllKeys can release them (menu open, hotswap) and
           a still-held trigger re-asserts the frame after. */
        InjectKey(K_JOY_LTRIG, l_ana > TRIGGER_THRESHOLD ? qtrue : qfalse);
        InjectKey(K_JOY_RTRIG, r_ana > TRIGGER_THRESHOLD ? qtrue : qfalse);

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

#if WPAD_ENABLED

static void WM_NunchukMovement(const struct joystick_t *js)
{
    float mag = js->mag;
    if (mag < NUNCHUK_DEADZONE) {
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

static void WM_IRAiming(const struct ir_t *ir, qboolean in_game)
{
    if (!ir->valid && !ir->smooth_valid) {
        s_ir_was_valid = qfalse;
        wii_ir_aim_x = wii_ir_aim_y = 0.0f;
        return;
    }

    float ix = ir->smooth_valid ? ir->sx : ir->x;
    float iy = ir->smooth_valid ? ir->sy : ir->y;

    if (in_game) {
        float dx = ix - IR_CENTER_X;
        float dy = iy - IR_CENTER_Y;

        /* Layer 2: normalized fine-aim offset, clamped to [-1, 1] */
        float nx = dx / IR_CENTER_X;
        float ny = dy / IR_CENTER_Y;
        if (nx >  1.0f) nx =  1.0f;
        if (nx < -1.0f) nx = -1.0f;
        if (ny >  1.0f) ny =  1.0f;
        if (ny < -1.0f) ny = -1.0f;
        wii_ir_aim_x = nx;
        wii_ir_aim_y = ny;

        float dz = ir_deadzone ? ir_deadzone->value : 40.0f;
        if (dx > -dz && dx < dz) dx = 0.0f;
        if (dy > -dz && dy < dz) dy = 0.0f;

        if (dx == 0.0f && dy == 0.0f)
            return;

        float sens  = ir_sensitivity ? ir_sensitivity->value : 0.15f;
        float maxd  = ir_maxDelta    ? ir_maxDelta->value    : 25.0f;
        dx *= sens;
        dy *= sens;

        if (dx >  maxd) dx =  maxd;
        if (dx < -maxd) dx = -maxd;
        if (dy >  maxd) dy =  maxd;
        if (dy < -maxd) dy = -maxd;

        int mx = (int)dx;
        int my = (int)dy;
        if (mx != 0 || my != 0)
            Com_QueueEvent(0, SE_MOUSE, mx, my, 0, NULL);
    } else {
        wii_ir_aim_x = wii_ir_aim_y = 0.0f;
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

    SetClassicBindings();

    if (!in_game) {
        u32 menu_held = data->btns_h;

        if (!MenuRawLayer(s_cc_buttons, (int)CC_BTN_COUNT, &menu_held,
                          WPAD_CLASSIC_BUTTON_MINUS, K_JOY10)) {
            for (i = 0; i < (int)CC_MENU_BTN_COUNT; i++)
                InjectKey(s_cc_menu_buttons[i].q3key,
                          (menu_held & s_cc_menu_buttons[i].bit) ? qtrue : qfalse);
        }

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

/* Normalise a raw DRC axis (~ -128..+127) to [-1, 1] with a fractional
   deadzone. Returns 0 inside the deadzone. */
static float DRC_NormAxis(s16 raw)
{
    float v = (float)raw / DRC_STICK_RANGE;
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    if (v > -DRC_STICK_DEADZONE && v < DRC_STICK_DEADZONE)
        return 0.0f;
    return v;
}

static void DRC_Input_Frame(const struct WiiDRCData *drc, qboolean in_game)
{
    int i;

    SetDRCBindings();

    /* HOME on the GamePad, or its POWER overlay / shutdown request, triggers
       the same clean quit-to-HBC path the Wiimote HOME button uses. */
    if ((drc->button & WIIDRC_BUTTON_HOME) || WiiDRC_ShutdownRequested())
        s_home_pressed = qtrue;

    if (!in_game) {
        u32 menu_held = drc->button;

        if (MenuRawLayer(s_drc_buttons, (int)DRC_BTN_COUNT, &menu_held,
                         WIIDRC_BUTTON_MINUS, K_JOY10)) {
            /* Plus emitted its JOY code above; keep the edge shadow current
               so raw-exit doesn't fake a menu-toggle edge. */
            s_drc_plus_prev = (drc->button & WIIDRC_BUTTON_PLUS) ? qtrue : qfalse;
        } else {
            for (i = 0; i < (int)DRC_MENU_BTN_COUNT; i++)
                InjectKey(s_drc_menu_buttons[i].q3key,
                          (menu_held & s_drc_menu_buttons[i].bit) ? qtrue : qfalse);

            {
                qboolean plus_now = (menu_held & WIIDRC_BUTTON_PLUS) ? qtrue : qfalse;
                if (plus_now && !s_drc_plus_prev)
                    InjectKey(K_ESCAPE, qtrue);
                else if (!plus_now && s_drc_plus_prev)
                    InjectKey(K_ESCAPE, qfalse);
                s_drc_plus_prev = plus_now;
            }
        }

        /* Left stick drives the menu cursor. */
        float lx = DRC_NormAxis(drc->xAxisL);
        float ly = DRC_NormAxis(drc->yAxisL);
        if (lx != 0.0f || ly != 0.0f) {
            s_accum_x += lx * MENU_SENSITIVITY_F;
            s_accum_y += -ly * MENU_SENSITIVITY_F; /* stick-up = cursor-up */
            int ox = (int)s_accum_x;
            int oy = (int)s_accum_y;
            s_accum_x -= (float)ox;
            s_accum_y -= (float)oy;
            if (ox != 0 || oy != 0)
                Com_QueueEvent(0, SE_MOUSE, ox, oy, 0, NULL);
        } else {
            s_accum_x = s_accum_y = 0.0f;
        }
        return;
    }

    s_drc_plus_prev = (drc->button & WIIDRC_BUTTON_PLUS) ? qtrue : qfalse;

    for (i = 0; i < (int)DRC_BTN_COUNT; i++)
        InjectKey(s_drc_buttons[i].q3key,
                  (drc->button & s_drc_buttons[i].bit) ? qtrue : qfalse);

    /* Left stick -> strafe / forward */
    float lx = DRC_NormAxis(drc->xAxisL);
    float ly = DRC_NormAxis(drc->yAxisL);
    short side = (short)(lx * DRC_STICK_SCALE);
    /* DRC yAxisL is +up; negate so the engine's j_forward=-0.25 flip yields
       forward on stick-up (matches the menu cursor, which also uses -ly). */
    short fwd  = (short)(-ly * DRC_STICK_SCALE);
    if (side != s_old_axis[0]) {
        Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_SIDE, side, 0, NULL);
        s_old_axis[0] = side;
    }
    if (fwd != s_old_axis[1]) {
        Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_FORWARD, fwd, 0, NULL);
        s_old_axis[1] = fwd;
    }

    /* Right stick -> yaw / pitch */
    float rx = DRC_NormAxis(drc->xAxisR);
    float ry = DRC_NormAxis(drc->yAxisR);
    short yaw   = (short)(rx * DRC_STICK_SCALE);
    short pitch = (short)(-ry * DRC_STICK_SCALE); /* stick-up = look up */
    if (yaw != s_old_axis[2] || yaw == 0) {
        Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_YAW, yaw, 0, NULL);
        s_old_axis[2] = yaw;
    }
    if (pitch != s_old_axis[3] || pitch == 0) {
        Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_PITCH, pitch, 0, NULL);
        s_old_axis[3] = pitch;
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
            wii_diag("[wpad] classic type=%d lmag=%.2f lang=%.1f rmag=%.2f rang=%.1f btns=0x%08x\n",
                     (int)data->exp.classic.type,
                     data->exp.classic.ljs.mag, data->exp.classic.ljs.ang,
                     data->exp.classic.rjs.mag, data->exp.classic.rjs.ang,
                     data->btns_h);
        }
    }
#endif

    if (has_classic) {
        /* CC handshake races the format request - retrigger once on first
           detect or the Wiimote lies and reports no expansion forever. */
        if (!s_cc_fmt_triggered) {
            s_cc_fmt_triggered = qtrue;
            WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
        }
        CC_Input_Frame(data, in_game);
    } else if (!in_game) {
        /* CC not present — reset so we retrigger format if CC is re-attached */
        s_cc_fmt_triggered = qfalse;

        SetWiimoteBindings();

        u32 menu_held = held;

        if (MenuRawLayer(s_wm_buttons, (int)WM_BTN_COUNT, &menu_held,
                         WPAD_BUTTON_MINUS, K_JOY6)) {
            /* Plus emitted its JOY code above; keep the edge shadow current
               so raw-exit doesn't fake a menu-toggle edge. */
            s_wm_plus_prev = (held & WPAD_BUTTON_PLUS) ? qtrue : qfalse;
        } else {
            for (i = 0; i < (int)WM_MENU_BTN_COUNT; i++)
                InjectKey(s_wm_menu_buttons[i].q3key,
                          (menu_held & s_wm_menu_buttons[i].bit) ? qtrue : qfalse);

            {
                qboolean plus_now = (menu_held & WPAD_BUTTON_PLUS) ? qtrue : qfalse;
                if (plus_now && !s_wm_plus_prev)
                    InjectKey(K_ESCAPE, qtrue);
                else if (!plus_now && s_wm_plus_prev)
                    InjectKey(K_ESCAPE, qfalse);
                s_wm_plus_prev = plus_now;
            }

            if (has_nunchuk)
                InjectKey(K_MOUSE1,
                          (menu_held & WPAD_NUNCHUK_BUTTON_Z) ? qtrue : qfalse);
        }

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

        SetWiimoteBindings();

        s_wm_plus_prev = (held & WPAD_BUTTON_PLUS) ? qtrue : qfalse;

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

/* Wired USB HID pad, brand-independent — wii_usb_hid.c already normalized
   PS3/PS4/DualSense into one virtual-gamepad layout before it gets here. */
static const btn_map_t s_usb_buttons[] = {
    { USBHID_BTN_A,      K_JOY1  },
    { USBHID_BTN_B,      K_JOY2  },
    { USBHID_BTN_X,      K_JOY3  },
    { USBHID_BTN_Y,      K_JOY4  },
    { USBHID_BTN_LB,     K_JOY5  },
    { USBHID_BTN_RB,     K_JOY6  },
    { USBHID_BTN_BACK,   K_JOY7  },
    { USBHID_BTN_START,  K_JOY8  },
    { USBHID_BTN_L3,     K_JOY9  },
    { USBHID_BTN_R3,     K_JOY10 },
    { USBHID_BTN_DUP,    K_JOY11 },
    { USBHID_BTN_DDOWN,  K_JOY12 },
    { USBHID_BTN_DLEFT,  K_JOY13 },
    { USBHID_BTN_DRIGHT, K_JOY14 },
};
#define USB_BTN_COUNT (sizeof(s_usb_buttons) / sizeof(s_usb_buttons[0]))

static const btn_map_t s_usb_menu_buttons[] = {
    { USBHID_BTN_A,      K_ENTER      },
    { USBHID_BTN_B,      K_ESCAPE     },
    { USBHID_BTN_RB,     K_MOUSE1     },
    { USBHID_BTN_DUP,    K_UPARROW    },
    { USBHID_BTN_DDOWN,  K_DOWNARROW  },
    { USBHID_BTN_DLEFT,  K_LEFTARROW  },
    { USBHID_BTN_DRIGHT, K_RIGHTARROW },
};
#define USB_MENU_BTN_COUNT (sizeof(s_usb_menu_buttons) / sizeof(s_usb_menu_buttons[0]))

#define USB_STICK_DEADZONE 4000   /* raw units out of -32767..32767 */

static void SetUSBBindings(void)
{
    if (s_active_ctrl_type == CTRL_TYPE_USB)
        return;

    qboolean force = SetActiveControllerType(CTRL_TYPE_USB);

    ApplyBind(K_JOY1,          "+moveup",     force); /* A/Cross = jump */
    ApplyBind(K_JOY2,          "+movedown",   force); /* B/Circle = crouch */
    ApplyBind(K_JOY3,          "weapnext",    force); /* X/Square */
    ApplyBind(K_JOY4,          "weapprev",    force); /* Y/Triangle */
    ApplyBind(K_JOY5,          "+speed",      force); /* LB = walk */
    ApplyBind(K_JOY6,          "+button2",    force); /* RB = use item */
    ApplyBind(K_JOY7,          "+scores",     force); /* Back/Share */
    ApplyBind(K_JOY8,          "togglemenu",  force); /* Start/Options */
    ApplyBind(K_JOY11,         "+forward",    force); /* D-up */
    ApplyBind(K_JOY12,         "+back",       force); /* D-down */
    ApplyBind(K_JOY13,         "+moveleft",   force); /* D-left */
    ApplyBind(K_JOY14,         "+moveright",  force); /* D-right */
    ApplyBind(K_JOY_USB_LTRIG, "+zoom",       force); /* LT */
    ApplyBind(K_JOY_USB_RTRIG, "+attack",     force); /* RT = fire */
    Key_SetBinding(K_MOUSE1, "");
}

static void USBPad_Input_Frame(qboolean in_game)
{
    int i;
    u16 buttons = USBHID_GetButtonMask();
    s16 lx, ly, rx, ry;
    u8  lt, rt;

    USBHID_GetAxes(&lx, &ly, &rx, &ry, &lt, &rt);

    SetUSBBindings();

    if (!in_game) {
        u32 menu_held = buttons;

        if (MenuRawLayer(s_usb_buttons, (int)USB_BTN_COUNT, &menu_held,
                         USBHID_BTN_BACK, K_JOY7)) {
            if (lt > TRIGGER_THRESHOLD || rt > TRIGGER_THRESHOLD)
                s_menu_mod_used = qtrue;
            InjectKey(K_JOY_USB_LTRIG, lt > TRIGGER_THRESHOLD ? qtrue : qfalse);
            InjectKey(K_JOY_USB_RTRIG, rt > TRIGGER_THRESHOLD ? qtrue : qfalse);
            /* Start emitted its JOY code above; keep the edge shadow current
               so raw-exit doesn't fake a menu-toggle edge. */
            s_usb_start_prev = (buttons & USBHID_BTN_START) ? qtrue : qfalse;
        } else {
            for (i = 0; i < (int)USB_MENU_BTN_COUNT; i++)
                InjectKey(s_usb_menu_buttons[i].q3key,
                          (menu_held & s_usb_menu_buttons[i].bit) ? qtrue : qfalse);

            {
                qboolean start_now = (menu_held & USBHID_BTN_START) ? qtrue : qfalse;
                if (start_now && !s_usb_start_prev)
                    InjectKey(K_ESCAPE, qtrue);
                else if (!start_now && s_usb_start_prev)
                    InjectKey(K_ESCAPE, qfalse);
                s_usb_start_prev = start_now;
            }
        }

        if (lx > USB_STICK_DEADZONE || lx < -USB_STICK_DEADZONE ||
            ly > USB_STICK_DEADZONE || ly < -USB_STICK_DEADZONE) {
            /* Do NOT negate ly - USBHID_GetAxes() already flipped it. See the
               in-game block below; I only need to explain this bug once. */
            s_accum_x += ((float)lx / 32767.0f) * MENU_SENSITIVITY_F;
            s_accum_y += ((float)ly / 32767.0f) * MENU_SENSITIVITY_F;
            int ox = (int)s_accum_x;
            int oy = (int)s_accum_y;
            s_accum_x -= (float)ox;
            s_accum_y -= (float)oy;
            if (ox != 0 || oy != 0)
                Com_QueueEvent(0, SE_MOUSE, ox, oy, 0, NULL);
        } else {
            s_accum_x = s_accum_y = 0.0f;
        }
        return;
    }

    s_usb_start_prev = (buttons & USBHID_BTN_START) ? qtrue : qfalse;

    for (i = 0; i < (int)USB_BTN_COUNT; i++)
        InjectKey(s_usb_buttons[i].q3key,
                  (buttons & s_usb_buttons[i].bit) ? qtrue : qfalse);

    /* Same InjectKey routing as the GC triggers — see that comment. */
    InjectKey(K_JOY_USB_LTRIG, lt > TRIGGER_THRESHOLD ? qtrue : qfalse);
    InjectKey(K_JOY_USB_RTRIG, rt > TRIGGER_THRESHOLD ? qtrue : qfalse);

    /* Do NOT add -ly/-ry like GC/CC/DRC do - USB HID Y is already "up=negative"
       coming out of _Parse(). Added it once anyway, swapped both sticks on HW. */
    short side  = (lx > -USB_STICK_DEADZONE && lx < USB_STICK_DEADZONE) ? 0 : lx;
    short fwd   = (ly > -USB_STICK_DEADZONE && ly < USB_STICK_DEADZONE) ? 0 : ly;
    short yaw   = (rx > -USB_STICK_DEADZONE && rx < USB_STICK_DEADZONE) ? 0 : rx;
    short pitch = (ry > -USB_STICK_DEADZONE && ry < USB_STICK_DEADZONE) ? 0 : ry;

    if (side != s_old_axis[0]) {
        Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_SIDE, side, 0, NULL);
        s_old_axis[0] = side;
    }
    if (fwd != s_old_axis[1]) {
        Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_FORWARD, fwd, 0, NULL);
        s_old_axis[1] = fwd;
    }
    if (yaw != s_old_axis[2] || yaw == 0) {
        Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_YAW, yaw, 0, NULL);
        s_old_axis[2] = yaw;
    }
    if (pitch != s_old_axis[3] || pitch == 0) {
        Com_QueueEvent(0, SE_JOYSTICK_AXIS, AXIS_PITCH, pitch, 0, NULL);
        s_old_axis[3] = pitch;
    }
}

static qboolean s_kb_inited  = qfalse;
static qboolean s_mouse_inited = qfalse;

static int KB_SymToQ3Key(u16 sym)
{
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

static void USB_Keyboard_Frame(void)
{
    keyboard_event evt;
    int max_events = 32;

    while (max_events-- > 0 && KEYBOARD_GetEvent(&evt) > 0) {
        if (evt.type == KEYBOARD_PRESSED || evt.type == KEYBOARD_RELEASED) {
            qboolean down = (evt.type == KEYBOARD_PRESSED) ? qtrue : qfalse;

            /* Grave/tilde is the standard console key, but it doesn't exist on
             * every layout (e.g. Italian keyboards), so F1 is also a toggle. */
            if (down && (evt.symbol == KS_grave || evt.symbol == KS_asciitilde ||
                         evt.symbol == KS_f1 || evt.symbol == KS_F1)) {
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

static u8 s_mouse_old_buttons = 0;

static void USB_Mouse_Frame(void)
{
    mouse_event evt;
    int dx = 0, dy = 0;
    u8 cur_buttons = s_mouse_old_buttons;
    int max_events = 64;

    while (max_events-- > 0 && MOUSE_GetEvent(&evt) > 0) {
        dx += evt.rx;
        dy += evt.ry;
        cur_buttons = evt.button;

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

void Wii_Input_Init(void)
{
    memset(&s_input, 0, sizeof(s_input));
    s_home_pressed     = qfalse;
    s_in_game          = qfalse;
    s_active_ctrl_type = -1;
    s_accum_x = s_accum_y = 0.0f;
    s_accum_cx = s_accum_cy = 0.0f;
    memset(s_old_axis, 0, sizeof(s_old_axis));

    PAD_Init();

#if WPAD_ENABLED
    s_cc_fmt_triggered = qfalse;
    s_ir_last_x = IR_CENTER_X;
    s_ir_last_y = IR_CENTER_Y;
    s_ir_was_valid = qfalse;

    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_0, 640, 480);
    WPAD_SetIdleTimeout(300); /* 5 minutes before auto-disconnect */

    /* Wii U GamePad. Safe no-op on a real Wii: Init returns false when the
       DRC I2C signature isn't present (non-vWii or unpatched fw). */
    WiiDRC_Init();
#ifdef WII_DEBUG
    printf("[input] WiiDRC_Init -> inited=%d\n", (int)WiiDRC_Inited());
#endif

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

    /* Wired USB HID pad init is deliberately NOT called here — see
       Wii_Input_USBHIDInit() below. */
}

/* Deliberately NOT called from Wii_Input_Init() - the raw ogc/usb.h stack
   full-on hangs the console if poked that early. Call after Wii_Net_Init(). */
void Wii_Input_USBHIDInit(void)
{
    USBHID_Init();
}

void Wii_Input_SetCvars(void)
{
    /* Register controller-type tracker so the value persists across reboots. */
    Cvar_Get("wii_lastControllerType", "0", CVAR_ARCHIVE);

    /* Axis wiring and joystick enable — always forced; not user-tunable. */
    Cvar_Set("in_joystick",          "1");
    Cvar_Set("in_joystickUseAnalog", "1");
    Cvar_Set("j_side_axis",    "0");
    Cvar_Set("j_forward_axis", "1");
    Cvar_Set("j_pitch_axis",   "3");
    Cvar_Set("j_yaw_axis",     "4");

    /* CVAR_ARCHIVE so these survive reboots in q3config.cfg. */
    Cvar_Get("j_side",    "0.25",   CVAR_ARCHIVE);
    Cvar_Get("j_forward", "-0.25",  CVAR_ARCHIVE);
    /* Force these - a stale pre-cl_sensitivity-scaling q3config.cfg produces
       absurd turn speed otherwise, and CVAR_ARCHIVE Cvar_Get won't override it. */
    Cvar_Set("j_pitch",   "0.002");
    Cvar_Set("j_yaw",     "-0.002");

    /* Disable blob shadows (refit flicker on non-flat floors); user cfg can override. */
    Cvar_Get("cg_shadows", "0", CVAR_ARCHIVE);

#if WPAD_ENABLED
    /* Wiimote IR cvars, tuned for once-per-frame polling. User cfg overrides. */
    ir_deadzone    = Cvar_Get("wii_ir_deadzone",    "40",   CVAR_ARCHIVE);
    ir_sensitivity = Cvar_Get("wii_ir_sensitivity", "0.30", CVAR_ARCHIVE);
    ir_maxDelta    = Cvar_Get("wii_ir_maxdelta",    "50",   CVAR_ARCHIVE);
    ir_yawRange    = Cvar_Get("wii_ir_yawrange",    "50",   CVAR_ARCHIVE);
    ir_pitchRange  = Cvar_Get("wii_ir_pitchrange",  "30",   CVAR_ARCHIVE);
#endif

    /* Set VM mode: 1=interpreter, 2=JIT. vm_ui set in cmdline (CL_InitUI runs early). */
#if defined(WII_VM_NATIVE)
    Cvar_Set("vm_ui",    "2");
    Cvar_Set("vm_cgame", "2");
    Cvar_Set("vm_game",  "2");
#else
    Cvar_Set("vm_ui",    "1");
    Cvar_Set("vm_cgame", "1");
    Cvar_Set("vm_game",  "1");
#endif

    /* Lower connect timeout: 200s too long without console feedback. */
    Cvar_Set("cl_timeout", "15");

    Cvar_Set("cg_drawFPS",      "0");
    Cvar_Set("cg_drawTimer",    "0");
    Cvar_Set("cg_drawSnapshot", "0");
    Cvar_Set("com_speeds",      "0");
    Cvar_Set("r_speeds",        "0");

    /* Avoid map_restart race on slow VMs. */
    Cvar_Set("g_doWarmup", "0");

    /* Only override name if it's still the default; preserve user customisation */
    {
        const char *n = Cvar_VariableString("name");
        if ( !n[0] || !Q_stricmp(n, "UnnamedPlayer") ) {
            Cvar_Set("name", "Quake3Wii");
        }
    }
}

int Wii_Input_GetCtrlType(void)
{
    return s_active_ctrl_type;
}

void Wii_Input_Frame(void)
{
    /* Poll for hotplugged USB pad (throttled ~1x/sec, polling-based). */
    USBHID_Poll();

    /* USB HID pad takes priority over DRC (user plugged it in deliberately). */
    if (USBHID_Active()) {
        qboolean in_game = (Key_GetCatcher() == 0) ? qtrue : qfalse;
        if (in_game != s_in_game) {
            ReleaseAllKeys();
            s_in_game = in_game;
        }
        s_home_pressed = qfalse; /* no HOME-equivalent on 3rd-party pads */

        USBPad_Input_Frame(in_game);

        if (s_kb_inited)
            USB_Keyboard_Frame();
        if (s_mouse_inited)
            USB_Mouse_Frame();
        return;
    }

#if WPAD_ENABLED
    /* Wii U GamePad (DRC): vWii-only, no-op on real Wii. */
    if (WiiDRC_Inited() && WiiDRC_Connected()) {
        WiiDRC_ScanPads();
        const struct WiiDRCData *drc = WiiDRC_Data();
        if (drc) {
            s_home_pressed = qfalse;

            qboolean in_game = (Key_GetCatcher() == 0) ? qtrue : qfalse;
            if (in_game != s_in_game) {
                ReleaseAllKeys();
                s_in_game = in_game;
            }

            DRC_Input_Frame(drc, in_game);

            if (s_kb_inited)
                USB_Keyboard_Frame();
            if (s_mouse_inited)
                USB_Mouse_Frame();
            return;
        }
    }

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

void Wii_Input_Shutdown(void)
{
    if (s_active_ctrl_type != -1) {
        SaveControllerBindings(s_active_ctrl_type);
        s_active_ctrl_type = -1;
    }
    USBHID_Shutdown();
}
