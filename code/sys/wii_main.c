/* ioquake3-wii: Wii platform entry point */

static unsigned char s_mainStack[512 * 1024] __attribute__((aligned(8)));
void *__ppc_main_sp __attribute__((section(".sdata"))) = &s_mainStack[sizeof(s_mainStack)];

#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <asndlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"

#include "../sys/wii_glimp.h"
#include "../input/wii_input.h"
#include "../audio/wii_snd.h"
#include "../sys/wii_net.h"
#include "keycodes.h"
#include "GL/gl.h"
#include "renderercommon/tr_types.h"
#include "renderercommon/tr_public.h"
extern refexport_t *GetRefAPI(int apiVersion, refimport_t *rimp);

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

static void Wii_InitConsole(void)
{
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb   = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();
}

static qboolean Wii_MountSD(void)
{
    if (!fatInitDefault()) {
        printf("[wii] fatInitDefault() failed – no SD card?\n");
        return qfalse;
    }
#ifdef WII_DEBUG
    printf("[wii] SD card mounted OK\n");
#endif
    chdir("sd:/quake3");
    return qtrue;
}

extern u32 Wii_MEM2_Init(void);

static void wii_power_cb(void)              { exit(0); }
static void wii_reset_cb(u32 irq, void *ctx){ (void)irq; (void)ctx; exit(0); }

#ifdef WII_DEBUG
void crash_mark(const char *msg)
{
    FILE *f = fopen("sd:/quake3/crash.txt", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}
#define CRASHLOG(fmt, ...) do { char _cb[256]; snprintf(_cb, sizeof(_cb), fmt, ##__VA_ARGS__); crash_mark(_cb); } while(0)
void boot_mark(const char *msg)
{
    FILE *f = fopen("sd:/quake3/boot.txt", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}
#define WII_DBG_PRINTF(...) do { printf(__VA_ARGS__); fflush(stdout); } while(0)
#else
void crash_mark(const char *msg) { (void)msg; }
#define CRASHLOG(...) ((void)0)
void boot_mark(const char *msg) { (void)msg; }
#define WII_DBG_PRINTF(...) ((void)0)
#endif

int main(int argc, char *argv[])
{
    Wii_InitConsole();
    WII_DBG_PRINTF("ioquake3-wii starting...\n");
    u32 mem2_bump_mb = Wii_MEM2_Init();
    WII_DBG_PRINTF("[wii] MEM2 init done (%u MB bump)\n", (unsigned)mem2_bump_mb);

    if (!Wii_MountSD()) {
        printf("FATAL: could not mount SD. Halting.\n");
        while (1) VIDEO_WaitVSync();
    }

#ifdef WII_DEBUG
    { FILE *f = fopen("sd:/quake3/boot.txt", "w"); if (f) fclose(f); }
    boot_mark("main() reached, SD mounted");
    {
        char _mem2msg[64];
        snprintf(_mem2msg, sizeof(_mem2msg), "MEM2 bump: %u MB", (unsigned)mem2_bump_mb);
        boot_mark(_mem2msg);
    }
    { FILE *f = fopen("sd:/quake3/crash.txt", "w"); if (f) fclose(f); }
    CRASHLOG("main() started");
#endif

    extern void Wii_InitMallocLock(void);
    Wii_InitMallocLock();

    Wii_Input_Init();
    WII_DBG_PRINTF("[wii] Input OK\n");
    boot_mark("Input init done");

    {
        int net_result = Wii_Net_Init();
        (void)net_result;
        WII_DBG_PRINTF("[wii] Network %s\n", net_result == 0 ? "OK" : "failed");
        boot_mark(net_result == 0 ? "Network OK" : "Network failed");
    }

    Wii_Snd_Init();
    WII_DBG_PRINTF("[wii] Audio OK\n");
    boot_mark("Audio init done");

    u32 hunk_mb = (mem2_bump_mb > 1) ? mem2_bump_mb - 1 : mem2_bump_mb;

    static char cmdline[1024];
    snprintf(cmdline, sizeof(cmdline),
        "+set fs_basepath sd:/quake3 "
        "+set fs_homepath sd:/quake3 "
        "+set fs_steampath \"\" "
        "+set fs_gogpath \"\" "
        "+set com_basegame " WII_BASEGAME " "
        "+set com_hunkMegs %u "
        "+set com_zoneMegs 8 "
        ,
        (unsigned)hunk_mb);
    snprintf(cmdline + strlen(cmdline), sizeof(cmdline) - strlen(cmdline),
        "+set r_mode -1 "
        "+set r_picmip 2 "
        "+set r_dynamic 0 "
        "+set r_flares 0 "
        "+set r_fastsky 0 "
        "+set r_lodbias 1 "
        "+set r_subdivisions 20 "
        "+set r_simpleMipMaps 1 "
        "+set r_drawSun 0 "
        "+set r_primitives 2 "
        "+set com_maxfps 30 "
        "+set pmove_fixed 1 "
        "+set s_khz 22 "
#if defined(STANDALONETA)
        "+set com_soundMegs 2 "
#else
        "+set com_soundMegs 4 "
#endif
        "+set sv_pure 0 "
        "+set sv_maxclients 8 "

#if !defined(STANDALONEOA) && !defined(STANDALONETA)
        "+set com_standalone 0 "
#endif
        "+set cl_allowDownload 1 "
        "+set net_enabled 1 "
        "+set net_port 27961 "
        "+set fraglimit 0 "
        "+set timelimit 0 "
        "+set com_logfile 2 "
        "+set vm_ui 1"
#if defined(STANDALONETA)
        " +set fs_game missionpack"
#elif defined(WII_FSGAME)
        " +set fs_game " WII_FSGAME
#endif
    );

    SYS_SetPowerCallback(wii_power_cb);
    SYS_SetResetCallback(wii_reset_cb);

    {
        FILE *kf = fopen("sd:/quake3/qkey", "rb");
        if (kf) {
            fclose(kf);
        } else {
            kf = fopen("sd:/quake3/qkey", "wb");
            if (kf) {
                unsigned char buf[2048];
                for (int i = 0; i < 2048; i++) buf[i] = (unsigned char)(i & 0xFF);
                fwrite(buf, 1, 2048, kf);
                fclose(kf);
                WII_DBG_PRINTF("[wii] Created qkey file\n");
            }
        }
    }

    boot_mark("Calling GX init");
    WII_DBG_PRINTF("[wii] Calling Wii_GX_Init...\n");

    /* GX must be up before Com_Init — it starts rendering immediately */
    Wii_GX_Init();
    WII_DBG_PRINTF("[wii] GX init done\n");
    boot_mark("GX init done");

    /* Pre-init renderer so re.BeginFrame doesn't crash during Com_Init */
    extern refexport_t re;
    refexport_t *ref = GetRefAPI(REF_API_VERSION, NULL);
    if (ref) re = *ref;
    boot_mark("GetRefAPI done");

    WII_DBG_PRINTF("[wii] Calling Com_Init...\n");
    boot_mark("Calling Com_Init");
    Com_Init(cmdline);
    WII_DBG_PRINTF("[wii] Com_Init done\n");
    boot_mark("Com_Init done");
    Wii_Input_SetCvars();

    NET_Init();
    boot_mark("NET_Init done, entering main loop");

    while (1) {
        Wii_Input_Frame();

        if (Wii_Input_HomePressed()) {
            Com_Quit_f();
            break;
        }

        Com_Frame();
    }

    Wii_Snd_Shutdown();
    NET_Shutdown();
    return 0;
}
