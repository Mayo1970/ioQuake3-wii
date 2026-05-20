/* Wii Sys_* interface — replaces ioQ3's sys_unix.c / sys_win32.c */

#include <gccore.h>
#include <ogc/lwp_watchdog.h>   /* ticks_to_millisecs, gettime */
#include <wiiuse/wpad.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "renderercommon/tr_types.h"  /* glconfig_t */
#include "keycodes.h"
extern int Key_GetCatcher(void);
#define KEYCATCH_UI 4

#include "wii_glimp.h"
#include "../input/wii_input.h"
#include "../audio/wii_snd.h"

void Sys_Init(void)
{

}

void Sys_Quit(void)
{
    Wii_Snd_Shutdown();
    Wii_GX_Shutdown();
    exit(0);
}

int Sys_Milliseconds(void)
{
    static u64      s_base_ticks = 0;
    static qboolean s_base_set   = qfalse;

    if (!s_base_set) {
        s_base_ticks = gettime();
        s_base_set   = qtrue;
    }

    /* Compute elapsed ms from a base captured on first call so the result
     * always fits in a positive 32-bit int (avoids the (int) truncation that
     * wraps absolute gettime() into a negative number on Wii). */
    int ms = (int)ticks_to_millisecs(gettime() - s_base_ticks);

    return ms;
}

void Sys_Sleep(int msec)
{
    if (msec > 0) {
        usleep((useconds_t)msec * 1000);
    }
}

/* Events are injected via Com_QueueEvent() in Wii_Input_Frame() */
sysEvent_t Sys_GetEvent(void)
{
    sysEvent_t ev = { 0 };
    ev.evType = SE_NONE;
    return ev;
}

char *Sys_GetClipboardData(void)
{
    return NULL;
}

char *Sys_Cwd(void)
{
    static char cwd[MAX_OSPATH];
    getcwd(cwd, sizeof(cwd));
    return cwd;
}

qboolean Sys_RandomBytes(byte *string, int len)
{
    /* Seed from hardware tick counter + wall clock for per-boot uniqueness */
    static unsigned int seed = 0;
    static qboolean seeded = qfalse;
    if (!seeded) {
        u64 ticks = gettime();
        time_t now = time(NULL);
        seed = (unsigned int)(ticks ^ (ticks >> 32) ^ (unsigned int)now);
        if (seed == 0) seed = 0x1;
        seeded = qtrue;
    }
    for (int i = 0; i < len; i++) {
        seed = seed * 1664525u + 1013904223u;
        string[i] = (byte)(seed >> 24);
    }
    return qtrue;
}

char *Sys_DefaultBasePath(void)     { return "sd:/quake3"; }
char *Sys_DefaultInstallPath(void)  { return "sd:/quake3"; }
char *Sys_DefaultHomePath(void)     { return "sd:/quake3"; }

void *Sys_LoadDll(const char *name,
                  intptr_t (**entryPoint)(int, ...),
                  intptr_t (*systemcalls)(intptr_t, ...))
{
    (void)name; (void)entryPoint; (void)systemcalls;
    return NULL;
}

void Sys_UnloadDll(void *dllHandle)
{
    (void)dllHandle;
}

char **Sys_ListFiles(const char *directory, const char *extension,
                     char *filter, int *numfiles, qboolean wantsubs)
{
    DIR           *dir;
    struct dirent *entry;
    int            nfiles = 0;
    int            nalloc = 64;
    char         **list;
    char           search[MAX_OSPATH];

    *numfiles = 0;

    dir = opendir(directory);
    if (!dir)
        return NULL;

    list = (char **)Z_Malloc(nalloc * sizeof(char *));

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        if (extension) {
            size_t nlen = strlen(entry->d_name);
            size_t elen = strlen(extension);
            if (nlen < elen ||
                Q_stricmp(entry->d_name + nlen - elen, extension) != 0)
                continue;
        }

        /* Reserve one slot for the NULL sentinel */
        if (nfiles + 1 >= nalloc) {
            char **newlist;
            nalloc *= 2;
            newlist = (char **)Z_Malloc(nalloc * sizeof(char *));
            memcpy(newlist, list, nfiles * sizeof(char *));
            Z_Free(list);
            list = newlist;
        }
        list[nfiles++] = CopyString(entry->d_name);
    }
    closedir(dir);

    list[nfiles] = NULL;
    *numfiles = nfiles;
    return list;
}

void Sys_FreeFileList(char **list)
{
    if (!list)
        return;
    for (int i = 0; list[i]; i++)
        Z_Free(list[i]);
    Z_Free(list);
}

qboolean Sys_LowPhysicalMemory(void)
{
    return qtrue;
}

void Sys_Error(const char *error, ...)
{
    va_list ap;
    char msg[4096];
    va_start(ap, error);
    vsnprintf(msg, sizeof(msg), error, ap);
    va_end(ap);
    printf("\n\n\n");
    printf("=============================\n");
    printf("[FATAL ERROR]\n");
    printf("%s\n", msg);
    printf("=============================\n");
    printf("Press START to reboot.\n");
    fflush(stdout);
    while (1) {
        PAD_ScanPads();
        if (PAD_ButtonsDown(PAD_CHAN0) & PAD_BUTTON_START) exit(1);
        VIDEO_WaitVSync();
    }
}

void Sys_Print(const char *msg)
{
    fputs(msg, stdout);
    fflush(stdout);
}

cpuFeatures_t Sys_GetProcessorFeatures(void)
{
    return (cpuFeatures_t)0;
}

/* GX + OpenGX already initialised in Wii_GX_Init() before Com_Init */
void GLimp_Init(qboolean fixedFunction)
{
    (void)fixedFunction;

    GXRModeObj *rmode = Wii_GX_GetRMode();

    extern glconfig_t glConfig;

    glConfig.vidWidth      = rmode ? (int)rmode->fbWidth   : 640;
    glConfig.vidHeight     = rmode ? (int)rmode->efbHeight : 480;
    glConfig.windowAspect  = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
    glConfig.colorBits     = 24;
    glConfig.depthBits     = 24;
    glConfig.stencilBits   = 0;
    glConfig.deviceSupportsGamma     = qfalse;
    glConfig.textureCompression      = TC_NONE;
    glConfig.textureEnvAddAvailable  = qfalse;
    Q_strncpyz(glConfig.renderer_string, "OpenGX/Wii",  sizeof(glConfig.renderer_string));
    Q_strncpyz(glConfig.vendor_string,   "Nintendo",    sizeof(glConfig.vendor_string));
    Q_strncpyz(glConfig.version_string,  "GX 1.0",      sizeof(glConfig.version_string));
    Q_strncpyz(glConfig.extensions_string,
               "GL_ARB_multitexture GL_EXT_compiled_vertex_array",
               sizeof(glConfig.extensions_string));

}

void GLimp_Shutdown(void)
{
    Wii_GX_Shutdown();
}

void GLimp_EndFrame(void)
{
    Wii_GX_EndFrame();
}

void GLimp_SetGamma(unsigned char red[256],
                    unsigned char green[256],
                    unsigned char blue[256])
{
    (void)red; (void)green; (void)blue;
}

qboolean GLimp_SpawnRenderThread(void (*function)(void))
{
    (void)function;
    return qfalse;
}

void *GLimp_RendererSleep(void)      { return NULL; }
void  GLimp_FrontEndSleep(void)      { }
void  GLimp_WakeRenderer(void *data) { (void)data; }

#include <netdb.h>

void Sys_SetEnv(const char *name, const char *value)
{ if (value) setenv(name, value, 1); else unsetenv(name); }

char    *Sys_ConsoleInput(void)                         { return NULL; }
void     Sys_InitPIDFile(const char *d)                 { (void)d; }
void     Sys_RemovePIDFile(const char *d)               { (void)d; }
qboolean Sys_DllExtension(const char *n)                { return strstr(n, DLL_EXT) ? qtrue : qfalse; }
qboolean Sys_OpenFolderInFileManager(const char *p, qboolean c) { (void)p;(void)c; return qfalse; }
qboolean Sys_Mkdir(const char *p)               { return (mkdir(p,0755)==0 || errno==EEXIST) ? qtrue : qfalse; }
FILE    *Sys_FOpen(const char *p, const char *m)        { return fopen(p,m); }
FILE    *Sys_Mkfifo(const char *p)                      { (void)p; return NULL; }

char *Sys_DefaultHomeConfigPath(void) { return "sd:/quake3"; }
char *Sys_DefaultHomeDataPath(void)   { return "sd:/quake3"; }
char *Sys_DefaultHomeStatePath(void)  { return "sd:/quake3"; }
char *Sys_SteamPath(void)             { return ""; }
char *Sys_GogPath(void)               { return ""; }
char *Sys_MicrosoftStorePath(void)    { return ""; }

void *QDECL Sys_LoadGameDll(const char *name, vmMainProc *ep,
                              intptr_t (*sc)(intptr_t,...))
{ (void)name;(void)ep;(void)sc; return NULL; }


void Sys_GLimpInit(void)     { }
void Sys_GLimpSafeInit(void) { }

void IN_Init(void *windowData) { (void)windowData; }
void IN_Shutdown(void)         { }
void IN_Restart(void)          { }
void IN_Frame(void)            { Wii_Input_Frame(); }

/* Sound: snd_main.c is not compiled; we delegate to S_Base_* in snd_dma.c */
#include "client/snd_local.h"

extern qboolean S_Base_Init(soundInterface_t *si);
extern void     S_Update_(void);
extern void     S_Base_Shutdown(void);
extern void     S_Base_StartSound(vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx);
extern void     S_Base_StartLocalSound(sfxHandle_t sfx, int channelNum);
extern void     S_Base_StopAllSounds(void);
extern void     S_Base_StopLoopingSound(int entityNum);
extern void     S_Base_ClearLoopingSounds(qboolean killall);
extern void     S_Base_AddLoopingSound(int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx);
extern void     S_Base_AddRealLoopingSound(int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx);
extern void     S_Base_UpdateEntityPosition(int entityNum, const vec3_t origin);
extern void     S_Base_Respatialize(int entityNum, const vec3_t origin, vec3_t axis[3], int inwater);
extern sfxHandle_t S_Base_RegisterSound(const char *sample, qboolean compressed);
extern void     S_Base_BeginRegistration(void);
extern void     S_Base_ClearSoundBuffer(void);
extern void     S_Base_DisableSounds(void);
extern void     S_Base_StartBackgroundTrack(const char *intro, const char *loop);
extern void     S_Base_StopBackgroundTrack(void);
extern void     S_Base_Update(void);

/* Cvars normally owned by snd_main.c (not compiled on Wii) */
cvar_t *s_volume;
cvar_t *s_muted;
cvar_t *s_musicVolume;
cvar_t *s_doppler;

static soundInterface_t s_snd_if;

extern void boot_mark(const char *msg);
extern void S_CodecInit(void);

void S_Init(void)
{
    boot_mark("S_Init enter");
    s_volume      = Cvar_Get("s_volume",      "0.8",  CVAR_ARCHIVE);
    s_muted       = Cvar_Get("s_muted",       "0",    CVAR_ROM);
    s_musicVolume = Cvar_Get("s_musicvolume", "0.25", CVAR_ARCHIVE);
    s_doppler     = Cvar_Get("s_doppler",     "1",    CVAR_ARCHIVE);

    S_CodecInit();
    boot_mark("S_Init: codec registered");

    Com_Memset(&s_snd_if, 0, sizeof(s_snd_if));
    boot_mark("S_Init: calling S_Base_Init");
    S_Base_Init(&s_snd_if);
    boot_mark("S_Init done");
}
void S_Shutdown(void)
{
    if (s_snd_if.Shutdown)
        s_snd_if.Shutdown();
    Wii_Snd_Shutdown();
    Com_Memset(&s_snd_if, 0, sizeof(s_snd_if));
}
void        S_Update(void)                                             { S_Base_Update(); }
void        S_BeginRegistration(void)                                  { S_Base_BeginRegistration(); }
sfxHandle_t S_RegisterSound(const char *n, qboolean comp)             { return S_Base_RegisterSound(n, comp); }
void        S_StartSound(vec3_t o,int n,int c,sfxHandle_t h)          { S_Base_StartSound(o,n,c,h); }
void        S_StartLocalSound(sfxHandle_t h,int c)                    { S_Base_StartLocalSound(h,c); }
void        S_StopAllSounds(void)                                      { S_Base_StopAllSounds(); }
void        S_StopLoopingSound(int e)                                  { S_Base_StopLoopingSound(e); }
void        S_ClearLoopingSounds(qboolean k)                           { S_Base_ClearLoopingSounds(k); }
void        S_AddLoopingSound(int n,const vec3_t o,const vec3_t v,sfxHandle_t h)     { S_Base_AddLoopingSound(n,o,v,h); }
void        S_AddRealLoopingSound(int n,const vec3_t o,const vec3_t v,sfxHandle_t h) { S_Base_AddRealLoopingSound(n,o,v,h); }
void        S_UpdateEntityPosition(int n,const vec3_t o)              { S_Base_UpdateEntityPosition(n,o); }
void        S_Respatialize(int n,const vec3_t o,vec3_t ax[3],int i)   { S_Base_Respatialize(n,o,ax,i); }
void        S_ClearSoundBuffer(void)                                   { S_Base_ClearSoundBuffer(); }
void        S_DisableSounds(void)                                      { S_Base_DisableSounds(); }
void        S_StartBackgroundTrack(const char *i,const char *l)       { S_Base_StartBackgroundTrack(i,l); }
void        S_StopBackgroundTrack(void)                                { S_Base_StopBackgroundTrack(); }
extern void S_Base_RawSamples(int stream, int samples, int rate, int width, int s_channels, const byte *data, float volume, int entityNum);
void        S_RawSamples(int stream,int samples,int rate,int width,int channels,const byte *d,float v,int e){ S_Base_RawSamples(stream,samples,rate,width,channels,d,v,e); }

qboolean CL_VideoRecording(void)               { return qfalse; }
qboolean CL_OpenAVIForWriting(const char *f)   { (void)f; return qfalse; }
qboolean CL_CloseAVI(void)                     { return qfalse; }
void     CL_TakeVideoFrame(void)               { }
void     CL_WriteAVIVideoFrame(const byte *d,int s) { (void)d;(void)s; }
void     CL_WriteAVIAudioFrame(const byte *d,int s) { (void)d;(void)s; }

char *Com_MD5File(const char *f,int l,const char *p,int pl)
{ (void)f;(void)l;(void)p;(void)pl; return ""; }

/* getaddrinfo/freeaddrinfo — IPv4-only; libogc declares but doesn't implement */
#include <arpa/inet.h>
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    struct addrinfo *ai;
    struct sockaddr_in *sin;
    int family = hints ? hints->ai_family : AF_INET;
    (void)service;

    if (family != AF_INET && family != AF_UNSPEC)
        return EAI_FAMILY;

    ai = (struct addrinfo *)calloc(1, sizeof(struct addrinfo) + sizeof(struct sockaddr_in));
    if (!ai) return EAI_MEMORY;

    sin = (struct sockaddr_in *)(ai + 1);
    sin->sin_family = AF_INET;

    if (!node || !node[0]) {
        sin->sin_addr.s_addr = INADDR_ANY;
    } else if (inet_aton(node, &sin->sin_addr)) {
        /* numeric IP */
    } else {
        struct hostent *he = net_gethostbyname(node);
        if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
            free(ai);
            return EAI_NONAME;
        }
        memcpy(&sin->sin_addr, he->h_addr_list[0], he->h_length);
    }

    ai->ai_family   = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : SOCK_DGRAM;
    ai->ai_addrlen  = sizeof(struct sockaddr_in);
    ai->ai_addr     = (struct sockaddr *)sin;
    *res = ai;
    return 0;
}
void freeaddrinfo(struct addrinfo *ai)
{
    struct addrinfo *next;
    while (ai) { next = ai->ai_next; free(ai); ai = next; }
}
const char *gai_strerror(int e)
{
    switch (e) {
    case 0:           return "Success";
    case EAI_FAMILY:  return "Address family not supported";
    case EAI_MEMORY:  return "Out of memory";
    case EAI_NONAME:  return "Name or service not known";
    default:          return "Resolver error";
    }
}
int getnameinfo(const struct sockaddr *sa, socklen_t sl,
                char *h, socklen_t hl,
                char *sv, socklen_t svl, int f)
{
    (void)sl; (void)sv; (void)svl; (void)f;
    if (sa->sa_family == AF_INET && h && hl > 0) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
        if (inet_ntop(AF_INET, &sin->sin_addr, h, hl))
            return 0;
    }
    if (h && hl > 0) h[0] = '\0';
    return EAI_FAMILY;
}
int gethostname(char *n,size_t l)      { if(n&&l>0)n[0]='\0'; return 0; }

const struct in6_addr in6addr_any = {{ 0 }};

/* minizip file I/O callbacks using standard fopen */
#include "zlib.h"
#include "ioapi.h"

static voidpf wii_fopen(voidpf opaque, const char *filename, int mode)
{
    const char *fmode;
    (void)opaque;
    if      (mode & ZLIB_FILEFUNC_MODE_WRITE) fmode = "wb";
    else if (mode & ZLIB_FILEFUNC_MODE_READ)  fmode = "rb";
    else                                       fmode = "rb";
    return (voidpf)fopen(filename, fmode);
}
static uLong wii_fread(voidpf op, voidpf stream, void *buf, uLong size)
{ (void)op; return (uLong)fread(buf, 1, size, (FILE *)stream); }
static uLong wii_fwrite(voidpf op, voidpf stream, const void *buf, uLong size)
{ (void)op; return (uLong)fwrite(buf, 1, size, (FILE *)stream); }
static long  wii_ftell(voidpf op, voidpf stream)
{ (void)op; return ftell((FILE *)stream); }
static long  wii_fseek(voidpf op, voidpf stream, uLong offset, int origin)
{ (void)op; return fseek((FILE *)stream, (long)offset, origin); }
static int   wii_fclose(voidpf op, voidpf stream)
{ (void)op; return fclose((FILE *)stream); }
static int   wii_ferror(voidpf op, voidpf stream)
{ (void)op; return ferror((FILE *)stream); }

void fill_fopen_filefunc(zlib_filefunc_def *p)
{
    if (!p) return;
    p->zopen_file  = wii_fopen;
    p->zread_file  = wii_fread;
    p->zwrite_file = wii_fwrite;
    p->ztell_file  = wii_ftell;
    p->zseek_file  = wii_fseek;
    p->zclose_file = wii_fclose;
    p->zerror_file = wii_ferror;
    p->opaque      = NULL;
}

#include <sys/types.h>
mode_t umask(mode_t mask) { (void)mask; return 0022; }

/* Forward FIONBIO to net_ioctl */
int ioctl(int fd, int request, ...)
{
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    return net_ioctl(fd, request, arg);
}

/* mmap/munmap — route large allocs to MEM2 bump allocator, fall back to memalign */
#include <malloc.h>
void *wii_mem2_alloc(size_t size);

/* Pointers >= mem2_base came from the bump allocator and must not be free()'d */
static u8 *mem2_base = NULL;

static inline int is_mem2_ptr(void *p)
{
    return mem2_base != NULL && (u8 *)p >= mem2_base;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    void *p = wii_mem2_alloc(len);
    if (p) return p;
    p = memalign(32, len);
    return p ? p : (void *)-1;
}

int munmap(void *addr, size_t len)
{
    (void)len;
    if (!is_mem2_ptr(addr))
        free(addr);
    return 0;
}

unsigned int CON_LogWrite(const char *in) { (void)in; return 0; }
unsigned int CON_LogSize(void)            { return 0; }
unsigned int CON_LogRead(char *out, unsigned int outlen) { (void)out;(void)outlen; return 0; }

/* MEM2 bump allocator: top 33 MB for hunk, rest for sbrk */
static u8   *mem2_ptr  = NULL;
static u32   mem2_left = 0;

#define MEM2_BUMP_SIZE (33u * 1024u * 1024u)

void Wii_MEM2_Init(void)
{
    u8 *lo = (u8 *)SYS_GetArena2Lo();
    u8 *hi = (u8 *)SYS_GetArena2Hi();
    u32 total = (u32)(hi - lo);

    if (total >= MEM2_BUMP_SIZE) {
        mem2_base = hi - MEM2_BUMP_SIZE;
        mem2_ptr  = mem2_base;
        mem2_left = MEM2_BUMP_SIZE;
    } else {
        mem2_base = lo;
        mem2_ptr  = lo;
        mem2_left = total;
    }

    /* Cap sbrk heap so it can't grow into the bump region */
    SYS_SetArena2Hi(mem2_base);
}


void *wii_mem2_alloc(size_t size)
{
    size_t aligned = (size + 31) & ~31;
    if (aligned <= mem2_left && mem2_ptr) {
        void *p = mem2_ptr;
        mem2_ptr  += aligned;
        mem2_left -= aligned;
        return p;
    }
    return NULL;
}

/* Route large calloc to MEM2 bump allocator.
 * Threshold is 16 MB so the Q3 hunk (32 MB) lands in MEM2 bump while the
 * zone (~8 MB) and smaller allocs stay on the sbrk heap. */
extern void *__real_calloc(size_t nmemb, size_t size);

void *__wrap_calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    if (total >= 16u * 1024u * 1024u) {
        void *p = wii_mem2_alloc(total);
        if (p) {
            memset(p, 0, total);
            return p;
        }
    }
    return __real_calloc(nmemb, size);
}

void Wii_VM_Yield(void)
{
    static int s_yield_count = 0;
    if (++s_yield_count < 100) return;
    s_yield_count = 0;

    PAD_ScanPads();
    if (PAD_ButtonsDown(0) & PAD_BUTTON_START) {
        exit(0);
    }
}

void __wrap_CL_GenerateQKey(void)
{
}

/* VM_Call wrap — intercept CD key checks and UIMENU_NEED_CD (UI VM only).
 * Callnum enums overlap across VMs (e.g. BOTAI_START_FRAME = UI_HASUNIQUECDKEY = 10),
 * so we must check vm == uivm before applying UI-specific intercepts. */
#define WII_UI_SET_ACTIVE_MENU  7
#define WII_UI_HASUNIQUECDKEY   10
#define WII_UIMENU_MAIN         1
#define WII_UIMENU_NEED_CD      3

extern void *uivm;
intptr_t QDECL __real_VM_Call( void *vm, int callnum, ... );

intptr_t QDECL __wrap_VM_Call( void *vm, int callnum, ... )
{
    int args[12];
    va_list ap;
    va_start(ap, callnum);
    for (int i = 0; i < 12; i++) args[i] = va_arg(ap, int);
    va_end(ap);

    if (vm == uivm) {
        if (callnum == WII_UI_HASUNIQUECDKEY)
            return 1;
        if (callnum == WII_UI_SET_ACTIVE_MENU && args[0] == WII_UIMENU_NEED_CD)
            args[0] = WII_UIMENU_MAIN;
    }

    if (!vm)
        return 0;

    return __real_VM_Call(vm, callnum,
        args[0], args[1], args[2], args[3],
        args[4], args[5], args[6], args[7],
        args[8], args[9], args[10], args[11]);
}

/* Recursive LWP mutex for newlib malloc thread-safety */
#include <ogc/mutex.h>

static mutex_t s_malloc_mtx = LWP_MUTEX_NULL;

void Wii_InitMallocLock(void)
{
    if (s_malloc_mtx == LWP_MUTEX_NULL)
        LWP_MutexInit(&s_malloc_mtx, true);
}

void __wrap___malloc_lock(struct _reent *r)
{
    (void)r;
    if (s_malloc_mtx == LWP_MUTEX_NULL)
        LWP_MutexInit(&s_malloc_mtx, true);
    LWP_MutexLock(s_malloc_mtx);
}

void __wrap___malloc_unlock(struct _reent *r)
{
    (void)r;
    if (s_malloc_mtx != LWP_MUTEX_NULL)
        LWP_MutexUnlock(s_malloc_mtx);
}
