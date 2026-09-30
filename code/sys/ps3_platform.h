/* ps3_platform.h -- force-included in every translation unit (-include). */

#ifndef PS3_PLATFORM_H
#define PS3_PLATFORM_H

/* Cell PPU is big-endian; must land before q_platform.h. */
#ifndef __BIG_ENDIAN
#  define __BIG_ENDIAN 4321
#endif
#ifndef __LITTLE_ENDIAN
#  define __LITTLE_ENDIAN 1234
#endif
#ifndef __BYTE_ORDER
#  define __BYTE_ORDER __BIG_ENDIAN
#endif
#ifndef __FLOAT_WORD_ORDER
#  define __FLOAT_WORD_ORDER __BIG_ENDIAN
#endif

#pragma GCC diagnostic ignored "-Wattributes"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* libjpeg: "typedef long INT32" is 64-bit on PPC64 and corrupts decoding. */
#ifdef XMD_H
#  ifndef _BASETSD_H_
    typedef int             INT32;
#  endif
    typedef short           INT16;
#endif

#ifndef MAP_FAILED
#  define MAP_FAILED ((void *)-1)
#endif

/* PS3 has no mmap; all memory is RWX. */
#ifndef PROT_READ
#  define PROT_READ     1
#  define PROT_WRITE    2
#  define PROT_EXEC     4
#  define MAP_SHARED    1
#  define MAP_ANONYMOUS 2
#  define MAP_ANON      MAP_ANONYMOUS
#endif

static inline int mprotect(void *addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot; return 0;
}

/* newlib on PS3 has no 64-bit file offsets for unzip.c. */
#define IOAPI_NO_64BIT
#define IOAPI_NO_64     /* this minizip spells it without BIT */

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

/* PSL1GHT BSD sockets, IPv4 only. */
#define HAVE_SA_LEN          0
#undef  HAVE_SOCKADDR_SA_LEN
#define NET_ENABLE_IPV6      0

/* net_ip.c only -- pulling this everywhere collides with huffman.c's send(). */
#ifdef PS3_INCLUDE_NET
#include "sys/ps3_net.h"
#endif

/* ---- Paths -------------------------------------------------------------- */

#ifndef PS3_APPID
#define PS3_APPID "IORTCWPS3"
#endif
/* Everything (game data, config, saves, log) lives in the game's USRDIR. */
#define PS3_USRDIR   "/dev_hdd0/game/" PS3_APPID "/USRDIR"
#define PS3_LOG_PATH PS3_USRDIR "/iortcw-ps3.log"

/* ---- Memory ------------------------------------------------------------- */
/* Hunk/zone sizes for common.c (iortcw SP wants a 128 MB minimum hunk, which
 * does not fit in what lv2 leaves us). Integers, not strings. */
#define PS3_MIN_COMHUNKMEGS  32
#ifndef PS3_DEF_COMHUNKMEGS
#define PS3_DEF_COMHUNKMEGS  80
#endif
#ifndef PS3_DEF_COMZONEMEGS
#define PS3_DEF_COMZONEMEGS  24
#endif

/* Ceiling for com_soundMegs, enforced in SND_setup (snd_mem.c). 1 "meg" is
 * 1536 sndBuffers of 2064 bytes on this ABI: 6 -> 18.1 MB. */
#define PS3_MAX_COMSOUNDMEGS 6

/* Default screen fit (percent of the TV picture used): TVs crop the edges. */
#define PS3_DEFAULT_SCREENFIT "88"

/* ---- Frame profiler (ps3_prof.c) ------------------------------------------
 * Time spent per phase of each frame, reported in the log with the [fps]
 * line. PS3_ProfTick reads the PPU timebase (cheap, no syscall). */
enum {
    PS3_PROF_SV,        /* server: game logic, AI, bots */
    PS3_PROF_CL,        /* whole client frame (the ones below are inside it) */
    PS3_PROF_CGAME,     /* cgame + renderer front end (culling, sorting) */
    PS3_PROF_BACKEND,   /* renderer back end + ps3gl (building RSX commands);
                           with r_smp it runs on the render thread, in parallel,
                           and so do flipwait, fencewait, anim, shade, gl* */
    PS3_PROF_FLIPWAIT,  /* waiting for a free frame buffer (GPU behind) */
    PS3_PROF_FENCEWAIT, /* waiting for the GPU to release vertex ring memory */
    PS3_PROF_SOUND,     /* S_Update: mixing */
    PS3_PROF_IDLE,      /* com_maxfps sleep */
    PS3_PROF_RWAIT,     /* r_smp: game thread waiting for the render thread */
    PS3_PROF_ANIM,      /* inside backend: CPU skinning of MDS (character) models */
    PS3_PROF_SHADE,     /* inside backend: shading + drawing batches (RB_EndSurface) */
    PS3_PROF_GLDRAW,    /* inside shade: ps3gl DrawElements */
    PS3_PROF_GLCOPY,    /* inside gldraw: copying vertices into RSX memory */
    PS3_PROF_SURF,      /* inside backend: building surfaces into tess (anim is part) */
    PS3_PROF_BONES,     /* inside anim: R_CalcBones */
    PS3_PROF_COLORS,    /* inside shade: per-stage vertex colors (ComputeColors) */
    PS3_PROF_TCOORDS,   /* inside shade: per-stage texture coords (ComputeTexCoords) */
    PS3_PROF_COUNT
};
static inline unsigned long long PS3_ProfTick(void)
{
    unsigned long long tb;
    __asm__ volatile("mftb %0" : "=r"(tb));
    return tb;
}
void PS3_ProfAdd(int slot, unsigned long long ticks);
extern int ps3_prof_smp;     /* the render thread is up (set by ps3_glimp.c) */
/* one module syscall (VM_DllSyscall): args[0] is the call number */
void PS3_ProfSyscall(const char *module, const intptr_t *args, unsigned long long ticks);

/* ps3gl counters, reset every frame by the profiler */
typedef struct {
    unsigned draws;         /* DrawElements/DrawArrays + immediate flushes */
    unsigned verts;         /* vertices copied into the vertex ring */
    unsigned indices;       /* inline indices written to the command buffer */
    unsigned vring_peak;    /* bytes of the ring segment used */
    unsigned dropped;       /* draws dropped because the ring segment was full */
} ps3gl_stats_t;
extern ps3gl_stats_t ps3gl_stats;
extern unsigned ps3gl_tex_bytes;    /* RSX memory held by textures */
extern unsigned ps3gl_tex_failed;   /* texture allocations that did not fit */

#define USE_INTERNAL_SDL_HEADERS

/* PSL1GHT's COLOR_* macros collide with the engine's console color constants. */
#undef COLOR_BLACK
#undef COLOR_RED
#undef COLOR_GREEN
#undef COLOR_YELLOW
#undef COLOR_BLUE
#undef COLOR_CYAN
#undef COLOR_MAGENTA
#undef COLOR_WHITE
#undef COLOR_ORANGE

#endif /* PS3_PLATFORM_H */
