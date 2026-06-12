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

/* Storage device root: "sd:/quake3" on a normal Wii, "usb:/quake3" on Wii Mini. Detected in Wii_MountSD(). */
char wii_dev_root[32] = "sd:/quake3";

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

static qboolean Wii_FindDataRoot(void)
{
    /* Try sd: first, then usb: (Wii Mini). chdir() both probes and sets the cwd. */
    static const char *roots[] = { "sd:/quake3", "usb:/quake3" };
    int i;
    for (i = 0; i < (int)(sizeof(roots) / sizeof(roots[0])); i++) {
        if (chdir(roots[i]) == 0) {
            Q_strncpyz(wii_dev_root, roots[i], sizeof(wii_dev_root));
            return qtrue;
        }
    }
    return qfalse;
}

static qboolean Wii_MountSD(void)
{
    int attempt;

    /* Retry FAT init: USB enumerates slower than SD, normal Wii mounts first try. */
    for (attempt = 0; attempt < 20; attempt++) {
        if (fatInitDefault())
            break;
        usleep(150000); /* 150 ms - let USB enumerate */
    }
    if (attempt == 20) {
        printf("[wii] fatInitDefault() failed - no SD or USB FAT device\n");
        return qfalse;
    }

    for (attempt = 0; attempt < 10; attempt++) {
        if (Wii_FindDataRoot()) {
#ifdef WII_DEBUG
            printf("[wii] data root: %s\n", wii_dev_root);
#endif
            return qtrue;
        }
        usleep(100000); /* 100 ms */
    }
    printf("[wii] mounted, but no /quake3 dir on sd: or usb:\n");
    return qfalse;
}

extern u32 Wii_MEM2_Init(void);

static void wii_power_cb(void)              { exit(0); }
static void wii_reset_cb(u32 irq, void *ctx){ (void)irq; (void)ctx; exit(0); }

#ifdef WII_DEBUG
void crash_mark(const char *msg)
{
    char p[64]; snprintf(p, sizeof(p), "%s/crash.txt", wii_dev_root);
    FILE *f = fopen(p, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}
#define CRASHLOG(fmt, ...) do { char _cb[256]; snprintf(_cb, sizeof(_cb), fmt, ##__VA_ARGS__); crash_mark(_cb); } while(0)
void boot_mark(const char *msg)
{
    char p[64]; snprintf(p, sizeof(p), "%s/boot.txt", wii_dev_root);
    FILE *f = fopen(p, "a");
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
    { char p[64]; snprintf(p, sizeof(p), "%s/boot.txt", wii_dev_root); FILE *f = fopen(p, "w"); if (f) fclose(f); }
    boot_mark("main() reached, storage mounted");
    {
        char _mem2msg[64];
        snprintf(_mem2msg, sizeof(_mem2msg), "MEM2 bump: %u MB", (unsigned)mem2_bump_mb);
        boot_mark(_mem2msg);
    }
    { char p[64]; snprintf(p, sizeof(p), "%s/crash.txt", wii_dev_root); FILE *f = fopen(p, "w"); if (f) fclose(f); }
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
        "+set fs_basepath %s "
        "+set fs_homepath %s "
        "+set fs_steampath \"\" "
        "+set fs_gogpath \"\" "
        "+set com_basegame " WII_BASEGAME " "
        "+set com_hunkMegs %u "
        "+set com_zoneMegs 8 "
        ,
        wii_dev_root, wii_dev_root, (unsigned)hunk_mb);
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
        "+set com_maxfps " WII_MAXFPS_STR " "
        "+set pmove_fixed 1 "
        "+set s_khz 22 "
        "+set com_soundMegs 2 "
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
        /* vm_ui must be set before Com_Init; vm_cgame/vm_game set post-init in Wii_Input_SetCvars(). */
#if defined(WII_VM_NATIVE)
        "+set vm_ui 2"
#else
        "+set vm_ui 1"
#endif
#if defined(STANDALONETA)
        " +set fs_game missionpack"
#elif defined(WII_FSGAME)
        " +set fs_game " WII_FSGAME
#endif
    );

    SYS_SetPowerCallback(wii_power_cb);
    SYS_SetResetCallback(wii_reset_cb);

    {
        char qkeypath[64];
        snprintf(qkeypath, sizeof(qkeypath), "%s/qkey", wii_dev_root);
        FILE *kf = fopen(qkeypath, "rb");
        if (kf) {
            fclose(kf);
        } else {
            kf = fopen(qkeypath, "wb");
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

    /* GX must be up before Com_Init — renderer starts immediately. */
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
        Com_Frame();

        /* Check HOME latch set by Wii_Input_Frame() inside Com_Frame() — don't poll again. */
        if (Wii_Input_HomePressed()) {
            Com_Quit_f();
            break;
        }
    }

    Wii_Snd_Shutdown();
    NET_Shutdown();
    return 0;
}
