/* ps3_main.c -- PS3 entry point for iortcw SP.
 *
 * Boot steps are numbered in the log ([0]..[8]) so a black screen or a hang
 * still says how far it got.
 *
 * STAGE 1 (make STAGE=1): headless. Engine + server + linked-in qagame, no
 * video/input/sound. Loads every .bsp in maps/ one after another, logs
 * how much hunk each one needs, and quits. Validates the engine, endianness,
 * the pk3 filesystem and the game module before any renderer work. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sys/thread.h>
#include <sys/process.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"

#include "sys/sys_local.h"

#include "ps3_log.h"

#ifndef PS3_STAGE
#define PS3_STAGE 2
#endif

SYS_PROCESS_PARAM(1001, 0x100000);

#define PS3_GAME_STACK (2 * 1024 * 1024)

extern unsigned PS3_FreeUserMemMB(void);

static volatile int ps3_running = 1;

static void ps3_sysutil_callback(u64 status, u64 param, void *userdata)
{
    (void)param; (void)userdata;
    if (status == SYSUTIL_EXIT_GAME)
        ps3_running = 0;
}

void PS3_Pump(void)
{
    sysUtilCheckCallback();
}

int PS3_Running(void)
{
    return ps3_running;
}

/* [3] USRDIR must be writable: config, saves and this log live there. */
static qboolean PS3_CheckUsrdir(void)
{
    FILE *f = fopen(PS3_USRDIR "/write.test", "wb");

    if (!f)
        return qfalse;
    fclose(f);
    remove(PS3_USRDIR "/write.test");
    Sys_Mkdir(PS3_USRDIR "/" BASEGAME);
    return qtrue;
}

/* [4] Game data present + endianness sanity: a pk3 starts with "PK\3\4",
 * which read as a little-endian int is 0x04034b50. */
static qboolean PS3_CheckData(void)
{
    static const char *paks[] = { "pak0.pk3", "sp_pak1.pk3", "sp_pak2.pk3", "sp_pak3.pk3", "sp_pak4.pk3" };
    qboolean ok = qtrue;
    int i;

    for (i = 0; i < (int)ARRAY_LEN(paks); i++) {
        char path[MAX_OSPATH];
        unsigned char hdr[4] = { 0 };
        FILE *f;
        long size = -1;

        Com_sprintf(path, sizeof(path), PS3_USRDIR "/" BASEGAME "/%s", paks[i]);
        f = fopen(path, "rb");
        if (f) {
            if (fread(hdr, 1, 4, f) != 4)
                memset(hdr, 0, sizeof(hdr));
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fclose(f);
        }
        PS3_Logf("[4]   %-12s %s  size=%ld  magic=%02x%02x%02x%02x", paks[i],
                 f ? "found" : "MISSING", size, hdr[0], hdr[1], hdr[2], hdr[3]);
        if (!f)
            ok = qfalse;
        else if (i == 0) {
            int le = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (hdr[3] << 24);
            PS3_Logf("[4]   pk3 magic as little-endian int: 0x%08x (%s)", le,
                     le == 0x04034b50 ? "ok" : "WRONG");
        }
    }
    return ok;
}

#if PS3_STAGE == 1

extern int Hunk_MemoryRemaining(void);

/* Runs Com_Frame until the server is up on the requested map (or it fails).
 * The "map" command itself goes through the command buffer so it executes
 * inside Com_Frame, under its ERR_DROP setjmp. */
static qboolean PS3_Stage1_LoadMap(const char *map, int hunkTotalMB)
{
    int start = Sys_Milliseconds();
    int frames;
    qboolean up = qfalse;
    int remaining;

    PS3_Logf("[6] ---- map %s ----", map);
    Cbuf_AddText(va("map %s\n", map));

    for (frames = 0; frames < 400 && ps3_running; frames++) {
        PS3_Pump();
        Com_Frame();
        if (com_sv_running->integer &&
            !Q_stricmp(Cvar_VariableString("mapname"), map)) {
            if (!up) {
                up = qtrue;
                PS3_Logf("[6] %s: server up after %d frames, %d ms", map, frames,
                         Sys_Milliseconds() - start);
                frames = 340;   /* then run 60 more server frames */
            }
        } else if (up) {
            break;              /* dropped after starting */
        }
    }

    remaining = Hunk_MemoryRemaining();
    PS3_Logf("[6] %s: %s  hunk used %.1f MB of %d MB  free user mem ~%u MB",
             map, (up && com_sv_running->integer) ? "OK" : "FAILED",
             (hunkTotalMB * 1024.0f * 1024.0f - remaining) / (1024.0f * 1024.0f),
             hunkTotalMB, PS3_FreeUserMemMB());
    Cbuf_AddText("meminfo\n");
    Com_Frame();
    return up;
}

static void PS3_Stage1(void)
{
    char **maps;
    int numMaps, i, ok = 0;
    int hunkMB;

    maps = FS_ListFiles("maps", ".bsp", &numMaps);
    hunkMB = Cvar_VariableIntegerValue("com_hunkMegs");
    PS3_Logf("[6] stage 1: %d maps found, hunk %d MB", numMaps, hunkMB);

    for (i = 0; i < numMaps && ps3_running; i++) {
        char map[MAX_QPATH];

        COM_StripExtension(maps[i], map, sizeof(map));
        if (PS3_Stage1_LoadMap(map, hunkMB))
            ok++;
    }
    FS_FreeFileList(maps);

    PS3_Logf("[6] stage 1 done: %d of %d maps loaded", ok, numMaps);
}

#endif /* PS3_STAGE == 1 */

static char ps3_cmdline[1024];
static char ps3_hunkarg[64];

static void PS3_GameThread(void *arg)
{
    (void)arg;

    ps3_log("[1] game thread started");

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, ps3_sysutil_callback, NULL);
    ps3_log("[2] sysutil callback registered");

    if (!PS3_CheckUsrdir()) {
        ps3_log("[3] FATAL: " PS3_USRDIR " is not writable");
        sysThreadExit(1);
    }
    ps3_log("[3] USRDIR writable");

    if (!PS3_CheckData()) {
        ps3_log("[4] FATAL: game data missing, copy the pk3 files to " PS3_USRDIR "/" BASEGAME);
        sysThreadExit(1);
    }
    ps3_log("[4] game data found");

    Sys_PlatformInit();

#if PS3_STAGE == 1
    /* Measure, don't guess: give the hunk what is free minus the zone and a
     * margin for mallocs, so the log shows what each map really needs. */
    {
        int freeMB = (int)PS3_FreeUserMemMB();
        int hunkMB = freeMB - PS3_DEF_COMZONEMEGS - 24;

        if (hunkMB > 160)
            hunkMB = 160;
        if (hunkMB < PS3_MIN_COMHUNKMEGS)
            hunkMB = PS3_MIN_COMHUNKMEGS;
        PS3_Logf("[4] free ~%d MB -> com_hunkMegs %d", freeMB, hunkMB);
        Com_sprintf(ps3_hunkarg, sizeof(ps3_hunkarg), "+set com_hunkMegs %d ", hunkMB);
    }
#endif

    Com_sprintf(ps3_cmdline, sizeof(ps3_cmdline),
        "+set fs_basepath " PS3_USRDIR " "
        "+set fs_homepath " PS3_USRDIR " "
        "+set fs_steampath \"\" "
        "+set fs_gogpath \"\" "
#if PS3_STAGE == 1
        "+set dedicated 1 "
        "+set sv_fps 20 "
        "%s"
#endif
        "+set vm_game 0 +set vm_cgame 0 +set vm_ui 0 "
        "+set com_logfile 0"
#if PS3_STAGE == 1
        , ps3_hunkarg
#endif
        );

    ps3_log("[5] Com_Init");
    Com_Init(ps3_cmdline);
    PS3_Logf("[5] Com_Init done, free user mem ~%u MB", PS3_FreeUserMemMB());

#if PS3_STAGE == 1
    PS3_Stage1();
#else
#error "Stage 2 (client) is not written yet"
#endif

    ps3_log("[8] shutdown");
    SV_Shutdown("Server quit");
    Com_Shutdown();
    FS_Shutdown(qtrue);
    ps3_log("[8] shutdown done");
    sysThreadExit(0);
}

int main(int argc, char *argv[])
{
    sys_ppu_thread_t tid;
    u64 retval = 0;
    s32 ret;

    (void)argc; (void)argv;

    PS3_LogInit();
    PS3_Logf("[0] main reached: iortcw-ps3 stage %d, built " __DATE__ " " __TIME__, PS3_STAGE);

    ret = sysThreadCreate(&tid, PS3_GameThread, NULL, 1000, PS3_GAME_STACK,
                          THREAD_JOINABLE, "iortcw");
    if (ret != 0) {
        PS3_Logf("[1] FATAL: sysThreadCreate failed (0x%08x)", ret);
        PS3_LogShutdown();
        return 1;
    }
    sysThreadJoin(tid, &retval);

    PS3_Logf("exit code %d", (int)retval);
    PS3_LogShutdown();
    return (int)retval;
}
