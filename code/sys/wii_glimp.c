#include <gccore.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>

#include "wii_glimp.h"
#include "wii_platform.h"

#define GX_FIFO_SIZE    (256 * 1024)
#define NUM_FRAMEBUFFERS 2

static GXRModeObj  *s_rmode       = NULL;
static void        *s_framebuf[NUM_FRAMEBUFFERS] = { NULL, NULL };
static void        *s_gp_fifo     = NULL;
static int          s_fb_index    = 0;
static qboolean     s_initialised = qfalse;

qboolean Wii_GX_Init(void)
{
    if (s_initialised)
        return qtrue;

    VIDEO_Init();
#if defined(WII_240P) && WII_240P
#  if defined(WII_PAL) && WII_PAL
    s_rmode = &TVPal264Ds;
#  else
    s_rmode = &TVNtsc240Ds;
#  endif
#else
    s_rmode = VIDEO_GetPreferredMode(NULL);
#endif

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

    extern void ogx_initialize(void);
    extern void ogx_set_framebuffer_height(int);
    ogx_initialize();
    ogx_set_framebuffer_height((int)s_rmode->efbHeight);

    wii_diag("[glimp] viTVMode=0x%02x fbWidth=%u efbHeight=%u xfbHeight=%u viHeight=%u fb_height_set=%d\n",
             (unsigned)s_rmode->viTVMode,
             (unsigned)s_rmode->fbWidth,
             (unsigned)s_rmode->efbHeight,
             (unsigned)s_rmode->xfbHeight,
             (unsigned)s_rmode->viHeight,
             (int)s_rmode->efbHeight);

    s_initialised = qtrue;
    return qtrue;
}

void Wii_GX_EndFrame(void)
{
    extern int ogx_prepare_swap_buffers(void);
    ogx_prepare_swap_buffers();

    /* Async pipeline — no GX_DrawDone() stall; only WaitVSync blocks */
    GX_SetDrawDone();
    s_fb_index ^= 1;
    GX_CopyDisp(s_framebuf[s_fb_index], GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(s_framebuf[s_fb_index]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
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
