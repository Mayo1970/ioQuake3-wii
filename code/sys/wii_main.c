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
#include <sys/stat.h>
#include <dirent.h>

#ifdef CLASSIC
#include "zpack_classic_embedded.h"
#include "zlib.h"   /* crc32() for the extraction gate */
#endif

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

/* 16 pages (512 KB) FAT cache for Q3A release only; 4 pages (128 KB)
   elsewhere. CLASSIC stays at 4 - its embedded zpack is already memory-tight. */
#if defined(STANDALONETA) || defined(WII_MODSELECT) || defined(STANDALONEOA) || \
    defined(WII_DEBUG) || defined(CLASSIC)
#define WII_FAT_CACHE_PAGES 4
#else
#define WII_FAT_CACHE_PAGES 16
#endif
#define WII_FAT_SECTORS_PER_PAGE 64

static qboolean Wii_MountSD(void)
{
    int attempt;

    /* Retry FAT init: USB enumerates slower than SD, normal Wii mounts first try. */
    for (attempt = 0; attempt < 20; attempt++) {
        if (dvmInit(true, WII_FAT_CACHE_PAGES, WII_FAT_SECTORS_PER_PAGE))
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

static qboolean s_bootpad_cc_fmt_triggered = qfalse;

/* Boot-time input (video mode / mod select) before Com_Init. Includes CC-init race fix:
   re-trigger WPAD_SetDataFormat() on CC detect so WPAD_CLASSIC_BUTTON_* bits report. */
static void Wii_BootPad_Poll(qboolean *pUp, qboolean *pDown, qboolean *pLeft, qboolean *pRight, qboolean *pA)
{
    u32 gcDown, wmDown, exp_type;
    WPADData *wd;

    *pUp = *pDown = *pLeft = *pRight = *pA = qfalse;

    PAD_ScanPads();
    gcDown = PAD_ButtonsDown(0);
    if (gcDown & PAD_BUTTON_UP)    *pUp    = qtrue;
    if (gcDown & PAD_BUTTON_DOWN)  *pDown  = qtrue;
    if (gcDown & PAD_BUTTON_LEFT)  *pLeft  = qtrue;
    if (gcDown & PAD_BUTTON_RIGHT) *pRight = qtrue;
    if (gcDown & PAD_BUTTON_A)     *pA     = qtrue;

    WPAD_ScanPads();
    exp_type = WPAD_EXP_NONE;
    WPAD_Probe(WPAD_CHAN_0, &exp_type);
    wd = WPAD_Data(WPAD_CHAN_0);
    wmDown = (wd && wd->err == WPAD_ERR_NONE) ? wd->btns_d : 0;

    if (exp_type == WPAD_EXP_CLASSIC) {
        if (!s_bootpad_cc_fmt_triggered) {
            s_bootpad_cc_fmt_triggered = qtrue;
            WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
        }
        if (wmDown & WPAD_CLASSIC_BUTTON_UP)    *pUp    = qtrue;
        if (wmDown & WPAD_CLASSIC_BUTTON_DOWN)  *pDown  = qtrue;
        if (wmDown & WPAD_CLASSIC_BUTTON_LEFT)  *pLeft  = qtrue;
        if (wmDown & WPAD_CLASSIC_BUTTON_RIGHT) *pRight = qtrue;
        if (wmDown & WPAD_CLASSIC_BUTTON_A)     *pA     = qtrue;
    } else {
        /* CC unplugged / not (yet) detected - reset so a CC that shows up
           later still gets its one retrigger. */
        s_bootpad_cc_fmt_triggered = qfalse;
        if (wmDown & WPAD_BUTTON_UP)    *pUp    = qtrue;
        if (wmDown & WPAD_BUTTON_DOWN)  *pDown  = qtrue;
        if (wmDown & WPAD_BUTTON_LEFT)  *pLeft  = qtrue;
        if (wmDown & WPAD_BUTTON_RIGHT) *pRight = qtrue;
        if (wmDown & WPAD_BUTTON_A)     *pA     = qtrue;
    }
}

#ifdef WII_MODSELECT
#define WII_MODSEL_MAX_MODS 16

static char wii_selected_fsgame[MAX_QPATH] = "";

/* A directory only counts as a mod if it ships at least one *.pk3 - matches
   FS_Startup, which loads any *.pk3 by wildcard, not just "pakN". */
static qboolean Wii_DirHasPak(const char *dirpath)
{
    DIR *d = opendir(dirpath);
    struct dirent *de;
    qboolean found = qfalse;

    if (!d) return qfalse;
    while ((de = readdir(d)) != NULL) {
        size_t len = strlen(de->d_name);
        if (len < 5) continue; /* shortest possible "X.pk3" */
        if (Q_stricmp(de->d_name + len - 4, ".pk3") != 0) continue;
        /* Belt-and-suspenders: a card shared with a CLASSIC install could have
           this sitting in a mod folder. No real mod pak is ever named this. */
        if (Q_stricmp(de->d_name, "zpack-classic.pk3") == 0) continue;
        found = qtrue;
        break;
    }
    closedir(d);
    return found;
}

static int Wii_ScanModDirs(char names[][MAX_QPATH], int maxNames)
{
    DIR *d = opendir(wii_dev_root);
    struct dirent *de;
    int count = 0;

    if (!d) return 0;
    while ((de = readdir(d)) != NULL && count < maxNames) {
        char full[160];
        struct stat st;

        if (de->d_name[0] == '.') continue;
        if (Q_stricmp(de->d_name, "baseq3") == 0) continue;

        snprintf(full, sizeof(full), "%s/%s", wii_dev_root, de->d_name);
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (!Wii_DirHasPak(full)) continue;

        Q_strncpyz(names[count], de->d_name, MAX_QPATH);
        count++;
    }
    closedir(d);
    return count;
}

/* Runs on the raw libogc console - GX/VM/FS aren't up yet, so this can't
   use the real input stack (needs Com_QueueEvent, needs Com_Init). */
static void Wii_ModSelect_Run(void)
{
    char names[WII_MODSEL_MAX_MODS][MAX_QPATH];
    int  count = Wii_ScanModDirs(names, WII_MODSEL_MAX_MODS);
    int  totalOptions = count + 1; /* +1 for "baseq3 only" */
    int  sel = 0;
    int  i;

    wii_selected_fsgame[0] = '\0';
    if (count == 0)
        return; /* nothing to pick from - boot straight into baseq3 */

    for (;;) {
        qboolean pUp, pDown, pLeft, pRight, pA;

        printf("\x1b[2J\x1b[1;1H");
        printf("\n  ioquake3-wii - select mod\n\n");
        printf("  D-pad/stick: Up-Down    A: confirm\n\n");

        for (i = 0; i < count; i++)
            printf("  %s %s\n", (i == sel) ? ">" : " ", names[i]);
        printf("  %s %s\n", (sel == count) ? ">" : " ", "[ baseq3 only ]");

        VIDEO_WaitVSync();

        Wii_BootPad_Poll(&pUp, &pDown, &pLeft, &pRight, &pA);
        (void)pLeft; (void)pRight;

        if (pUp)   sel = (sel == 0) ? totalOptions - 1 : sel - 1;
        if (pDown) sel = (sel + 1) % totalOptions;
        if (pA)    break;
    }

    if (sel < count)
        Q_strncpyz(wii_selected_fsgame, names[sel], sizeof(wii_selected_fsgame));

    printf("\x1b[2J\x1b[1;1H");
    printf("\n  Starting: %s\n\n", wii_selected_fsgame[0] ? wii_selected_fsgame : "baseq3");
}
#endif /* WII_MODSELECT */

/* UP = VIDEO_GetPreferredMode() default, LEFT = 240p NTSC, RIGHT = 264p PAL.
   10s of no input falls back to default. */
static void Wii_VideoModeBootPrompt(void)
{
    const u32 DEADLINE_MS = 10000;
    u32 start = Sys_Milliseconds();

    wii_video_mode_choice = 0; /* default unless a direction is pressed */

    for (;;) {
        qboolean pUp, pDown, pLeft, pRight, pA;
        u32 elapsed = Sys_Milliseconds() - start;
        u32 remaining = (elapsed < DEADLINE_MS) ? (DEADLINE_MS - elapsed) : 0;

        printf("\x1b[2J\x1b[1;1H");
        printf("\n  Select video mode (%u s):\n\n", (unsigned)(remaining / 1000));
        printf("  UP    = Default\n");
        printf("  LEFT  = 240p (NTSC)\n");
        printf("  RIGHT = 264p (PAL)\n");

        VIDEO_WaitVSync();

        Wii_BootPad_Poll(&pUp, &pDown, &pLeft, &pRight, &pA);
        (void)pDown; (void)pA;

        if (pUp)    { wii_video_mode_choice = 0; break; }
        if (pLeft)  { wii_video_mode_choice = 1; break; }
        if (pRight) { wii_video_mode_choice = 2; break; }
        if (elapsed >= DEADLINE_MS) break; /* timeout -> default */
    }

    printf("\x1b[2J\x1b[1;1H");
    printf("\n  -> %s\n\n",
           wii_video_mode_choice == 0 ? "Default" :
           wii_video_mode_choice == 1 ? "240p NTSC" : "264p PAL");
}

extern u32 Wii_MEM2_Init(void);

#ifdef CLASSIC
static void Wii_ExtractBundledZpackClassic(void)
{
    char destdir[72], destpath[88];
    snprintf(destdir,  sizeof(destdir),  "%s/baseq3",              wii_dev_root);
    snprintf(destpath, sizeof(destpath), "%s/zpack-classic.pk3",   destdir);

    /* Skip only if the on-disk file matches the embedded copy exactly:
       same size (a longer file with a matching prefix is still stale) and
       same CRC32. */
    FILE *ef = fopen(destpath, "rb");
    if (ef) {
        long fsz = (fseek(ef, 0, SEEK_END) == 0) ? ftell(ef) : -1;
        if (fsz == (long)zpack_classic_data_len) {
            rewind(ef);
            unsigned char *buf = malloc(zpack_classic_data_len);
            if (buf) {
                size_t n = fread(buf, 1, zpack_classic_data_len, ef);
                qboolean match = (n == zpack_classic_data_len &&
                    crc32(crc32(0L, Z_NULL, 0), buf, (uInt)n) == zpack_classic_data_crc);
                free(buf);
                if (match) {
                    fclose(ef);
                    return;
                }
            }
        }
        fclose(ef);
    }

    mkdir(destdir, 0755);
    FILE *f = fopen(destpath, "wb");
    if (!f) { printf("[wii] CLASSIC: cannot write %s\n", destpath); return; }
    size_t wr = fwrite(zpack_classic_data, 1, zpack_classic_data_len, f);
    if (fclose(f) != 0 || wr != zpack_classic_data_len)
        printf("[wii] CLASSIC: short write on %s (SD full/removed?) — "
               "pk3 is incomplete, will re-extract next boot\n", destpath);
    else
        printf("[wii] CLASSIC: extracted zpack-classic.pk3\n");
}
#endif

/* Power/reset callbacks run in IOS callback context — never exit() there
   (blocking-in-callback hazard, and it would skip IN_Shutdown's binding
   save). Latch a flag, same pattern as HOME; the main loop consumes it. */
static volatile qboolean s_power_requested = qfalse;
static volatile qboolean s_reset_requested = qfalse;
qboolean wii_poweroff_requested = qfalse;   /* read by Sys_Quit (wii_sys.c) */
static void wii_power_cb(void)              { s_power_requested = qtrue; }
static void wii_reset_cb(u32 irq, void *ctx){ (void)irq; (void)ctx; s_reset_requested = qtrue; }

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

#ifdef CLASSIC
    Wii_ExtractBundledZpackClassic();
#endif

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

    Wii_VideoModeBootPrompt();
    boot_mark("Video mode selected");

#ifdef WII_MODSELECT
    Wii_ModSelect_Run();
    boot_mark(wii_selected_fsgame[0] ? "Mod selected" : "Mod select: baseq3 only");
#endif

    {
        int net_result = Wii_Net_Init();
        (void)net_result;
        WII_DBG_PRINTF("[wii] Network %s\n", net_result == 0 ? "OK" : "failed");
        boot_mark(net_result == 0 ? "Network OK" : "Network failed");
    }

    /* Deferred until after Wii_Net_Init() - see wii_input.c for why calling
       this any earlier hangs the console outright. */
    Wii_Input_USBHIDInit();
    boot_mark("USB HID pad init done");

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
        /* Keep the "r_dynamic" typo - the real cvar hard-crashes release
           builds (debug boots fine, unhelpfully). See CLAUDE.md before touching this. */
        "+set r_dynamic 0 "
        "+set r_flares 0 "
        "+set r_fastsky 0 "
        /* r_lodbias deliberately absent - it's dead on Wii, don't waste a slot on it. */
        "+set r_gamma 1.3 "
        "+set r_subdivisions 20 "
        "+set r_simpleMipMaps 0 "
        "+set r_drawSun 0 "
        "+set r_primitives 2 "
        "+set com_maxfps " WII_MAXFPS_STR " "
        "+set pmove_fixed 1 "
        "+set s_khz 22 "
        "+set com_soundMegs 2 "
        "+set sv_pure 0 "
        /* No "sv_maxclients 8" - the default already matches, and we're at
           the 31-line MAX_CONSOLE_LINES cap. Recount before adding a token back. */

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
#elif defined(WII_FSGAME) && !defined(WII_MODSELECT)
        /* Skipped under WII_MODSELECT - the runtime picker below appends its own
           fs_game slot, and stacking both overflows the 31-line cap silently. */
        " +set fs_game " WII_FSGAME
#endif
    );

#ifdef WII_MODSELECT
    /* Selected at the boot-time picker above; empty means baseq3 only. */
    if (wii_selected_fsgame[0]) {
        snprintf(cmdline + strlen(cmdline), sizeof(cmdline) - strlen(cmdline),
                  " +set fs_game %s", wii_selected_fsgame);
    }
#endif

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

        /* HOME latch is set by Wii_Input_Frame() inside Com_Frame() — don't
           poll again. Power/reset latches come from the SYS callbacks. */
        if (s_power_requested || s_reset_requested || Wii_Input_HomePressed()) {
            wii_poweroff_requested = s_power_requested;
            Com_Quit_f();   /* clean shutdown; ends in Sys_Quit, never returns */
            break;
        }
    }

    Wii_Snd_Shutdown();
    NET_Shutdown();
    return 0;
}
