/* Force-included before every TU via -include */

#ifndef WII_PLATFORM_H
#define WII_PLATFORM_H

/* Fallback: define __linux__ so q_platform.h doesn't #error on unknown OS */
#if !defined(__linux__) && !defined(WIN32) && !defined(MACOS_X) && \
    !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(GEKKO)
#  define __linux__
#endif

/* Wii is big-endian; define before q_platform.h evaluates byte order */
#ifndef __BIG_ENDIAN
#  define __BIG_ENDIAN 4321
#endif
#ifndef __BYTE_ORDER
#  define __BYTE_ORDER __BIG_ENDIAN
#endif
#undef  Q3_LITTLE_ENDIAN
#define Q3_BIG_ENDIAN

#ifndef OS_STRING
#  define OS_STRING "wii"
#endif
#ifndef ARCH_STRING
#  define ARCH_STRING "ppc"
#endif

/* In-game framerate cap injected into the boot cmdline (wii_main.c). The
 * Makefile passes -DWII_MAXFPS_STR via the WII_MAXFPS flag (default "30");
 * this fallback keeps the source compilable if built outside the Makefile.
 * 30 is stable on real Wii; 60 is for Wii U / vWii (faster CPU). Menus/loading
 * always run at 60 regardless (see CL_InMenu in common.c). */
#ifndef WII_MAXFPS_STR
#  define WII_MAXFPS_STR "30"
#endif

/* q_platform.h never defines HAVE_VM_COMPILED for the Wii (no GEKKO arch arm;
 * the __linux__ fallback that would is disabled because GEKKO is defined). So
 * the PPC JIT (code/qcommon/vm_powerpc.c) is normally compiled OUT and vm.c
 * forces VMI_BYTECODE regardless of the vm_* cvars (it prints "Architecture
 * doesn't have a bytecode compiler, using interpreter"). Define it here (gated
 * on the WII_VM_NATIVE build flag) to compile the JIT path in for experimental
 * native-PPC QVM builds. Default builds leave it undefined → interpreter. */
#if defined(WII_VM_NATIVE) && !defined(HAVE_VM_COMPILED)
#  define HAVE_VM_COMPILED
#endif
#ifndef PATH_SEP
#  define PATH_SEP '/'
#endif
#ifndef DLL_EXT
#  define DLL_EXT ".so"
#endif

/* Define early so q_platform.h skips its own */
#ifndef ID_INLINE
#  define ID_INLINE __inline__
#endif
#pragma GCC diagnostic ignored "-Wattributes"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifndef MAP_FAILED
#  define MAP_FAILED ((void *)-1)
#endif

/* Constants for vm_powerpc.c compiled QVM */
#ifndef PROT_READ
#  define PROT_READ     1
#  define PROT_WRITE    2
#  define PROT_EXEC     4
#  define MAP_SHARED    0x01
#  define MAP_ANONYMOUS 0x20
#  define MAP_ANON      MAP_ANONYMOUS
#endif

/* All Wii memory is executable */
static inline int mprotect(void *addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot; return 0;
}

/* timersub for vm_powerpc.c timing printout (libogc has gettimeofday already) */
#include <sys/time.h>
#ifndef timersub
#define timersub(a, b, res) do { \
    (res)->tv_sec  = (a)->tv_sec  - (b)->tv_sec;  \
    (res)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
    if ((res)->tv_usec < 0) { (res)->tv_sec--; (res)->tv_usec += 1000000; } \
} while (0)
#endif

#define IOAPI_NO_64BIT

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

/* No ifaddrs.h, no IPv6 on Wii */
#define HAVE_SA_LEN          0
#undef  HAVE_SOCKADDR_SA_LEN
#define NET_ENABLE_IPV6      0

#ifdef WII_INCLUDE_NET
#include "sys/wii_net.h"
#endif

/*
 * Netchan memory tuning. MAX_RELIABLE_COMMANDS must stay at stock 64:
 * servers with custom content burst >16 reliable commands on connect,
 * causing CL_AddReliableCommand to Com_Error(ERR_DROP).
 */
#ifndef MAX_RELIABLE_COMMANDS
#define MAX_RELIABLE_COMMANDS   64      /* must be power-of-2 */
#endif
#ifndef PACKET_BACKUP
#define PACKET_BACKUP           16      /* stock 32; must be power-of-2 */
#endif
#ifndef PACKET_MASK
#define PACKET_MASK             (PACKET_BACKUP-1)
#endif
#ifndef MAX_DOWNLOAD_WINDOW
#define MAX_DOWNLOAD_WINDOW     48
#endif

/* Override ioQ3 hunk/zone minimums for Wii's 88 MB total RAM */
#undef  MIN_DEDICATED_COMHUNKMEGS
#undef  MIN_COMHUNKMEGS
#undef  DEF_COMHUNKMEGS
#undef  DEF_COMZONEMEGS
#define MIN_DEDICATED_COMHUNKMEGS 8
#define MIN_COMHUNKMEGS           8
#define DEF_COMHUNKMEGS           32
#define DEF_COMZONEMEGS           4

/* Reduce audio BSS: stock s_rawsamples is 129×16384×8 = 16 MB, too large for Wii */
#ifndef MAX_RAW_STREAMS
#define MAX_RAW_STREAMS  1      /* stock: MAX_CLIENTS*2+1 = 129 */
#endif
#ifndef MAX_RAW_SAMPLES
#define MAX_RAW_SAMPLES  16384  /* must stay at stock; smaller starves RoQ decoder bursts */
#endif

/* Undef libogc COLOR_* macros — ioQ3 redefines them as char literals */
#undef COLOR_BLACK
#undef COLOR_RED
#undef COLOR_GREEN
#undef COLOR_YELLOW
#undef COLOR_BLUE
#undef COLOR_CYAN
#undef COLOR_MAGENTA
#undef COLOR_WHITE
#undef COLOR_ORANGE

/* Diagnostic logging to sd:/quake3/diag.txt (WII_DEBUG only) */
#include <stdio.h>
#include <stdarg.h>
#ifdef WII_DEBUG
static inline void wii_diag(const char *fmt, ...) __attribute__((format(printf,1,2)));
static inline void wii_diag(const char *fmt, ...) {
    /* Keep one persistent handle open across calls. The previous version
     * re-opened the file (fopen) and closed it (fclose) on every call; that
     * directory-traversal + metadata I/O is a heavy blocking SD/FAT operation,
     * and when wii_diag fires mid-frame from the renderer it stalls the GX FIFO
     * and corrupts the displayed frame. Opening once removes that stall. We
     * still fflush every line so diag.txt stays complete up to a crash/freeze
     * (its whole purpose) — fflush on an already-open handle is far cheaper
     * than the open+close it replaces, and the renderer's wii_diag calls are
     * count-capped so the volume is bounded. */
    static FILE *f = NULL;
    if (!f) {
        extern char wii_dev_root[];   /* "sd:/quake3" or "usb:/quake3" (Wii Mini) */
        char path[64];
        snprintf(path, sizeof(path), "%s/diag.txt", wii_dev_root);
        f = fopen(path, "a");
        if (!f) return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fflush(f);
}
#else
static inline void wii_diag(const char *fmt, ...) { (void)fmt; }
#endif

#define USE_INTERNAL_SDL_HEADERS

#endif /* WII_PLATFORM_H */
