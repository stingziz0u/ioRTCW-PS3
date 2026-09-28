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
