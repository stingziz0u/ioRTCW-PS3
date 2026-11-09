/* ps3_prof.c -- where the frame time goes, in the log.
 *
 * The engine brackets each phase of the frame with PS3_ProfTick() and hands
 * the elapsed timebase ticks to PS3_ProfAdd(). Every frame PS3_ProfFrameEnd()
 * folds them into the 30 s window that ps3_main.c reports: the average of
 * all frames, the average of the slow ones (over 20 ms, under 50 fps) and the
 * breakdown of the worst frame, plus the ps3gl counters. */

#include <sys/systime.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"

#include "ps3_log.h"
#include "ps3_prof.h"
#include "ps3_prof_names.h"

static const char *const ps3_prof_names[PS3_PROF_COUNT] = {
    "sv", "cl", "cgame", "backend", "flipwait", "fencewait", "snd", "idle",
    "rwait", "anim", "shade", "gldraw", "glcopy", "surf", "bones", "colors", "tcoords"
};

ps3gl_stats_t ps3gl_stats;     /* filled by ps3gl (stage 2) */
unsigned ps3gl_tex_bytes, ps3gl_tex_failed;
int ps3_prof_smp;

static unsigned long long prof_cur[PS3_PROF_COUNT];     /* this frame */
static double prof_sum[PS3_PROF_COUNT];                 /* window, all frames (us) */
static double prof_slow[PS3_PROF_COUNT];                /* window, slow frames (us) */
static double prof_worst[PS3_PROF_COUNT];               /* the worst frame (us) */
static double prof_worst_frame;
static int prof_frames, prof_slow_frames;

static double gl_sum_draws, gl_sum_verts, gl_sum_indices;
static double gl_slow_draws, gl_slow_verts, gl_slow_indices;
static unsigned gl_worst_draws, gl_worst_verts;
static unsigned gl_peak_vring, gl_dropped;

static double prof_us_per_tick;

/* syscalls of the game (0) and cgame (1) modules, this window */
#define SC_MODULES 2
static unsigned long long sc_ticks[SC_MODULES][PS3_PROF_MAX_SYSCALLS];
static unsigned sc_calls[SC_MODULES][PS3_PROF_MAX_SYSCALLS];
static int prof_hitches;

static void PS3_ProfInitClock(void)
{
    if (prof_us_per_tick == 0.0) {
        unsigned long long f = sysGetTimebaseFrequency();
        prof_us_per_tick = 1e6 / (double)(f ? f : 79800000ULL);
    }
}

void PS3_ProfAdd(int slot, unsigned long long ticks)
{
    if (slot >= 0 && slot < PS3_PROF_COUNT)
        prof_cur[slot] += ticks;
}

static const char *PS3_SyscallName(int m, int num)
{
    const char *name = NULL;

    if (num >= 0 && num < PS3_PROF_MAX_SYSCALLS)
        name = m ? ps3_cg_syscall_names[num] : ps3_g_syscall_names[num];
    return name ? name : va("#%d", num);
}

void PS3_ProfSyscall(const char *module, const intptr_t *args, unsigned long long ticks)
{
    int m, num = (int)args[0];
    double ms;

    if (!module || (module[0] != 'q' && module[0] != 'c'))
        return;                             /* ui: not in the game loop */
    m = module[0] == 'c';
    if (num < 0 || num >= PS3_PROF_MAX_SYSCALLS)
        num = PS3_PROF_MAX_SYSCALLS - 1;
    sc_ticks[m][num] += ticks;
    sc_calls[m][num]++;

    /* a single call that stalls a frame: say which, and what it loaded */
    PS3_ProfInitClock();
    ms = ticks * prof_us_per_tick / 1000.0;
#ifndef PS3_DIAG
    prof_hitches = 40;      /* diagnostic builds only (make DIAG=1) */
#endif
    if (ms >= 10.0 && prof_hitches < 40) {
        const char *name = PS3_SyscallName(m, num);
        const char *what = "";

        prof_hitches++;
        if (strstr(name, "REGISTER") && !strstr(name, "CVAR") && args[1])
            what = (const char *)args[1];   /* native module: a real pointer */
        PS3_Logf("[hitch] %s %s %.1f ms %s", m ? "cgame" : "game", name, ms, what);
    }
}

void PS3_ProfFrameEnd(double frame_us)
{
    int i;
    int slow = frame_us > 20000.0;

    PS3_ProfInitClock();

    for (i = 0; i < PS3_PROF_COUNT; i++) {
        double us = prof_cur[i] * prof_us_per_tick;
        prof_sum[i] += us;
        if (slow)
            prof_slow[i] += us;
    }
    gl_sum_draws += ps3gl_stats.draws;
    gl_sum_verts += ps3gl_stats.verts;
    gl_sum_indices += ps3gl_stats.indices;
    if (slow) {
        gl_slow_draws += ps3gl_stats.draws;
        gl_slow_verts += ps3gl_stats.verts;
        gl_slow_indices += ps3gl_stats.indices;
        prof_slow_frames++;
    }
    if (frame_us > prof_worst_frame) {
        prof_worst_frame = frame_us;
        for (i = 0; i < PS3_PROF_COUNT; i++)
            prof_worst[i] = prof_cur[i] * prof_us_per_tick;
        gl_worst_draws = ps3gl_stats.draws;
        gl_worst_verts = ps3gl_stats.verts;
    }
    if (ps3gl_stats.vring_peak > gl_peak_vring)
        gl_peak_vring = ps3gl_stats.vring_peak;
    gl_dropped += ps3gl_stats.dropped;
    prof_frames++;

    memset(prof_cur, 0, sizeof(prof_cur));
    memset(&ps3gl_stats, 0, sizeof(ps3gl_stats));
}

static void PS3_ProfLine(const char *what, const double *us, double div,
                         double draws, double verts, double indices)
{
    char buf[768];
    int i, len;

    Com_sprintf(buf, sizeof(buf), "[prof] %s ms:", what);
    for (i = 0; i < PS3_PROF_COUNT; i++) {
        len = strlen(buf);
        Com_sprintf(buf + len, sizeof(buf) - len, " %s %.1f",
                    ps3_prof_names[i], us[i] / div / 1000.0);
    }
    len = strlen(buf);
    Com_sprintf(buf + len, sizeof(buf) - len, " | draws %.0f verts %.0f idx %.0f",
                draws / div, verts / div, indices / div);
    PS3_Logf("%s", buf);
}

/* the calls a module spent the most time in, ms per frame and calls per frame */
static void PS3_ProfSyscallLine(int m, double frames, double module_us)
{
    char buf[640];
    int used[PS3_PROF_MAX_SYSCALLS] = { 0 };
    double total = 0;
    int i, k, len;

    for (i = 0; i < PS3_PROF_MAX_SYSCALLS; i++)
        total += sc_ticks[m][i] * prof_us_per_tick;
    Com_sprintf(buf, sizeof(buf), "[prof] %s: own code %.1f ms, engine calls %.1f ms:",
                m ? "cgame" : "game", (module_us - total) / frames / 1000.0,
                total / frames / 1000.0);
    for (k = 0; k < 6; k++) {
        int best = -1;

        for (i = 0; i < PS3_PROF_MAX_SYSCALLS; i++)
            if (!used[i] && sc_calls[m][i] && (best < 0 || sc_ticks[m][i] > sc_ticks[m][best]))
                best = i;
        if (best < 0)
            break;
        used[best] = 1;
        len = strlen(buf);
        Com_sprintf(buf + len, sizeof(buf) - len, " %s %.2f (%.0f/f)",
                    PS3_SyscallName(m, best), sc_ticks[m][best] * prof_us_per_tick / frames / 1000.0,
                    sc_calls[m][best] / frames);
    }
    PS3_Logf("%s", buf);
}

void PS3_ProfReset(void)
{
    memset(sc_ticks, 0, sizeof(sc_ticks));
    memset(sc_calls, 0, sizeof(sc_calls));
    prof_hitches = 0;
    memset(prof_cur, 0, sizeof(prof_cur));
    memset(prof_sum, 0, sizeof(prof_sum));
    memset(prof_slow, 0, sizeof(prof_slow));
    memset(prof_worst, 0, sizeof(prof_worst));
    prof_worst_frame = 0;
    prof_frames = prof_slow_frames = 0;
    gl_sum_draws = gl_sum_verts = gl_sum_indices = 0;
    gl_slow_draws = gl_slow_verts = gl_slow_indices = 0;
    gl_worst_draws = gl_worst_verts = 0;
    gl_peak_vring = gl_dropped = 0;
}

void PS3_ProfReport(void)
{
    if (prof_frames > 0) {
        PS3_ProfLine(ps3_prof_smp ? "avg (smp)" : "avg",
                     prof_sum, prof_frames,
                     gl_sum_draws, gl_sum_verts, gl_sum_indices);
        if (prof_slow_frames > 0)
            PS3_ProfLine(va("slow frames (%d) avg", prof_slow_frames), prof_slow,
                         prof_slow_frames, gl_slow_draws, gl_slow_verts, gl_slow_indices);
        PS3_ProfLine(va("worst frame %.1f", prof_worst_frame / 1000.0), prof_worst, 1,
                     gl_worst_draws, gl_worst_verts, 0);
        PS3_Logf("[prof] vertex ring peak %u KB of %u KB per frame, %u draws dropped",
                 gl_peak_vring / 1024, PS3GL_VRING_SEGMENT_KB, gl_dropped);
        /* the cgame slot is CL_CGameRendering; the game module runs in sv */
        PS3_ProfSyscallLine(1, prof_frames, prof_sum[PS3_PROF_CGAME]);
        PS3_ProfSyscallLine(0, prof_frames, prof_sum[PS3_PROF_SV]);
    }

    PS3_ProfReset();
}
