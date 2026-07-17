#include <gccore.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>

#include "wii_glimp.h"
#include "wii_platform.h"
#if defined(WII_NATIVE_GX)
/* Bare extern - pulling in tr_gx.h's renderer types here is not worth it. */
extern void GXBE_FrameEnd(void);
#endif

#define GX_FIFO_SIZE    (256 * 1024)
#define NUM_FRAMEBUFFERS 2

static GXRModeObj  *s_rmode       = NULL;
static void        *s_framebuf[NUM_FRAMEBUFFERS] = { NULL, NULL };
static void        *s_gp_fifo     = NULL;
static int          s_fb_index    = 0;
static qboolean     s_initialised = qfalse;

int wii_video_mode_choice = 0; /* 0=default, 1=240p NTSC, 2=264p PAL */

#if defined(WII_GX_PROFILE) && WII_GX_PROFILE
#include <ogc/lwp_watchdog.h>

/* GP bottleneck profiler - tells you whether chasing the JIT is a waste of time (it is). */
#define GXPROF_WINDOW 128

static const struct { u32 p0, p1; const char *l0, *l1; } s_gxprof_cfg[] = {
    { GX_PERF0_XF_WAIT_OUT, GX_PERF1_CLOCKS,     "xf_wait_out_clk", "gp_clocks"      },
    { GX_PERF0_XF_WAIT_IN,  GX_PERF1_CLOCKS,     "xf_wait_in_clk",  "gp_clocks"      },
    { GX_PERF0_TRIANGLES,   GX_PERF1_FIFO_REQ,   "triangles",       "fifo_32B_lines" },
    { GX_PERF0_VERTICES,    GX_PERF1_CP_ALL_REQ, "vertices",        "cp_32B_reqs"    },
};
#define GXPROF_NCFG ((int)(sizeof(s_gxprof_cfg) / sizeof(s_gxprof_cfg[0])))

static int s_gxprof_cfgidx  = 0;
static int s_gxprof_frames  = 0;
static u64 s_gxprof_sum0    = 0;
static u64 s_gxprof_sum1    = 0;
static u64 s_gxprof_sum_us  = 0;
static u64 s_gxprof_last_tb = 0;

static void gxprof_init(void)
{
    GX_SetGPMetric(s_gxprof_cfg[0].p0, s_gxprof_cfg[0].p1);
    GX_ClearGPMetric();
    s_gxprof_last_tb = gettime();
    wii_diag("[gxprof] active: %d-frame windows, GP=243MHz (243000 clk/ms). "
             "xf_wait_out high => GP-bound (native GX won't help); "
             "xf_wait_out ~0 with large bk => CPU/feed-bound (native GX helps).\n",
             GXPROF_WINDOW);
}

static void gxprof_frame(void)
{
    u32 c0 = 0, c1 = 0;
    u64 now = gettime();

    GX_ReadGPMetric(&c0, &c1);
    GX_ClearGPMetric();

    s_gxprof_sum0   += c0;
    s_gxprof_sum1   += c1;
    s_gxprof_sum_us += ticks_to_microsecs(now - s_gxprof_last_tb);
    s_gxprof_last_tb = now;

    if (++s_gxprof_frames >= GXPROF_WINDOW) {
        u64 n     = (u64)s_gxprof_frames;
        u64 avg0  = s_gxprof_sum0 / n;
        u64 avg1  = s_gxprof_sum1 / n;
        u64 avgus = s_gxprof_sum_us / n;

        wii_diag("[gxprof] %s=%llu %s=%llu | frame=%lu.%02lums%s\n",
                 s_gxprof_cfg[s_gxprof_cfgidx].l0, (unsigned long long)avg0,
                 s_gxprof_cfg[s_gxprof_cfgidx].l1, (unsigned long long)avg1,
                 (unsigned long)(avgus / 1000),
                 (unsigned long)((avgus % 1000) / 10),
                 (s_gxprof_cfg[s_gxprof_cfgidx].p1 == GX_PERF1_CLOCKS)
                     ? " (gp_clocks/243000=gp_elapsed_ms)" : "");

        s_gxprof_sum0 = s_gxprof_sum1 = s_gxprof_sum_us = 0;
        s_gxprof_frames = 0;
        s_gxprof_cfgidx = (s_gxprof_cfgidx + 1) % GXPROF_NCFG;
        GX_SetGPMetric(s_gxprof_cfg[s_gxprof_cfgidx].p0,
                       s_gxprof_cfg[s_gxprof_cfgidx].p1);
        GX_ClearGPMetric();
    }
}
#endif /* WII_GX_PROFILE */

qboolean Wii_GX_Init(void)
{
    if (s_initialised)
        return qtrue;

    VIDEO_Init();
    switch (wii_video_mode_choice) {
        case 1:  s_rmode = &TVNtsc240Ds; break;
        case 2:  s_rmode = &TVPal264Ds;  break;
        default: s_rmode = VIDEO_GetPreferredMode(NULL); break;
    }

    s_framebuf[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(s_rmode));
    s_framebuf[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(s_rmode));

    VIDEO_Configure(s_rmode);
    VIDEO_SetNextFramebuffer(s_framebuf[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (s_rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();

    s_gp_fifo = memalign(32, GX_FIFO_SIZE);
    if (!s_gp_fifo) {
        printf("[glimp] FATAL: could not allocate GX FIFO\n");
        return qfalse;
    }
    memset(s_gp_fifo, 0, GX_FIFO_SIZE);
    GX_Init(s_gp_fifo, GX_FIFO_SIZE);

    GXColor bg = { 0, 0, 0, 255 };
    GX_SetCopyClear(bg, 0x00FFFFFF);

    float yscale = GX_GetYScaleFactor(s_rmode->efbHeight, s_rmode->xfbHeight);
    u32   xfbHeight = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, s_rmode->fbWidth, s_rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, s_rmode->fbWidth, s_rmode->efbHeight);
    GX_SetDispCopyDst(s_rmode->fbWidth, xfbHeight);
    GX_SetCopyFilter(s_rmode->aa, s_rmode->sample_pattern, GX_TRUE,
                     s_rmode->vfilter);
    GX_SetFieldMode(s_rmode->field_rendering,
                    ((s_rmode->viHeight == 2 * s_rmode->xfbHeight)
                        ? GX_ENABLE : GX_DISABLE));
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_CopyDisp(s_framebuf[s_fb_index], GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);

#if !defined(WII_NATIVE_GX)
    {
        extern void ogx_initialize(void);
        extern void ogx_set_framebuffer_height(int);
        ogx_initialize();
        ogx_set_framebuffer_height((int)s_rmode->efbHeight);
    }
#endif

    wii_diag("[glimp] viTVMode=0x%02x fbWidth=%u efbHeight=%u xfbHeight=%u viHeight=%u fb_height_set=%d\n",
             (unsigned)s_rmode->viTVMode,
             (unsigned)s_rmode->fbWidth,
             (unsigned)s_rmode->efbHeight,
             (unsigned)s_rmode->xfbHeight,
             (unsigned)s_rmode->viHeight,
             (int)s_rmode->efbHeight);

#if defined(WII_GX_PROFILE) && WII_GX_PROFILE
    gxprof_init();
#endif

    s_initialised = qtrue;
    return qtrue;
}

void Wii_GX_EndFrame(void)
{
#if !defined(WII_NATIVE_GX)
    /* Never call this under WII_NATIVE_GX - it stomps the staging-ring fences. */
    {
        extern int ogx_prepare_swap_buffers(void);
        ogx_prepare_swap_buffers();
    }
#endif

    /* No VIDEO_WaitVSync here - hard vsync halves FPS the instant a frame runs long. */
    s_fb_index ^= 1;
    GX_CopyDisp(s_framebuf[s_fb_index], GX_TRUE);
    GX_DrawDone();
#if defined(WII_GX_PROFILE) && WII_GX_PROFILE
    gxprof_frame();
#endif
#if defined(WII_NATIVE_GX)
    GXBE_FrameEnd();
#endif
    VIDEO_SetNextFramebuffer(s_framebuf[s_fb_index]);
    VIDEO_Flush();
}

void Wii_GX_Shutdown(void)
{
    if (!s_initialised)
        return;
    GX_AbortFrame();
    GX_Flush();
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    if (s_gp_fifo) { free(s_gp_fifo); s_gp_fifo = NULL; }
    s_initialised = qfalse;
}

GXRModeObj *Wii_GX_GetRMode(void)
{
    return s_rmode;
}

int Wii_GX_GetEFBHeight(void)
{
    return s_rmode ? (int)s_rmode->efbHeight : 480;
}

int Wii_GX_GetXFBHeight(void)
{
    return s_rmode ? (int)s_rmode->xfbHeight : 480;
}
