/* ps3_main.c -- PS3 entry point for iortcw SP.
 *
 * Boot steps are numbered in the log ([0]..[8]) so a black screen or a hang
 * still says how far it got.
 *
 * STAGE 1 (make STAGE=1): headless. Engine + server + linked-in qagame, no
 * video/input/sound. Loads every .bsp in maps/ one after another, logs
 * how much hunk each one needs, and quits. Validates the engine, endianness,
 * the pk3 filesystem and the game module before any renderer work.
 *
 * STAGE 2 (make): the game. RSX + ps3gl, pad, audio, qagame/cgame/ui. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sys/thread.h>
#include <sys/process.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"

#include "sys/sys_local.h"

#include "ps3_log.h"
#include "ps3_prof.h"
#include "ps3_rev.h"
#if PS3_STAGE >= 2
#include "ps3_glimp.h"
#include "../input/ps3_input.h"
#endif

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

#if PS3_STAGE >= 2
extern void (*PS3_ExitHook)(int code);
extern void SNDDMA_Shutdown(void);

/* Every way out (quit, XMB exit, fatal error) goes through here: the audio
 * thread and the RSX must be stopped before the process exits, or the
 * console can hang or reboot. */
static void PS3_Shutdown(int code)
{
    static int done;

    if (done)
        return;
    done = 1;
    PS3_Logf("[8] shutdown (code %d)", code);
    SNDDMA_Shutdown();
    PS3_Input_Shutdown();
    PS3_RSX_Shutdown();
    ps3_log("[8] shutdown done");
}
#endif

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

extern int Hunk_MemoryRemaining(void);

#if PS3_STAGE != 1
/* Frame rate in the log: every 30 s, average fps, the slowest 1 s window,
 * the longest frame and how many frames took over 20 ms (under 50 fps). */
#define PS3_STATS_WINDOW_US 30000000ULL

static u64 PS3_NowUs(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (u64)tv.tv_sec * 1000000ULL + (u64)tv.tv_usec;
}

/* hunk, malloc and RSX texture memory in one log line */
static void PS3_LogMem(const char *tag)
{
    PS3_Logf("%s hunk free %.1f MB, malloc free ~%u MB, RSX textures %.1f MB%s",
             tag, Hunk_MemoryRemaining() / (1024.0 * 1024.0), PS3_FreeUserMemMB(),
             ps3gl_tex_bytes / (1024.0 * 1024.0),
             ps3gl_tex_failed ? va(", %u textures did NOT fit", ps3gl_tex_failed) : "");
}

static void PS3_FrameStats(void)
{
    static u64 last, windowStart, secStart;
    static int frames, secFrames, minSecFrames, slow;
    static u64 worst;
    u64 now = PS3_NowUs();
    u64 dt;

    if (last == 0) {
        last = windowStart = secStart = now;
        minSecFrames = 1 << 30;
        return;
    }
    dt = now - last;
    last = now;
    PS3_ProfFrameEnd((double)dt);
    frames++;
    secFrames++;
    if (dt > worst)
        worst = dt;
    if (dt > 20000)
        slow++;
    if (now - secStart >= 1000000ULL) {
        if (secFrames < minSecFrames)
            minSecFrames = secFrames;
        secFrames = 0;
        secStart = now;
    }
#ifdef PS3_DIAG
    if (now - windowStart >= PS3_STATS_WINDOW_US) {
        const char *map = Cvar_VariableString("mapname");

        PS3_Logf("[fps] %.1f avg, slowest second %d, worst frame %.1f ms, "
                 "%d frames over 20 ms (map %s%s)",
                 frames * 1000000.0 / (double)(now - windowStart),
                 minSecFrames == (1 << 30) ? frames : minSecFrames,
                 worst / 1000.0, slow, map[0] ? map : "-",
                 Cvar_VariableIntegerValue("cl_paused") ? ", paused" : "");
        PS3_ProfReport();
        PS3_LogMem("[mem]");
        frames = slow = 0;
        worst = 0;
        minSecFrames = 1 << 30;
        windowStart = now;
    }
#endif
}
#endif

#if PS3_STAGE != 1 && defined(PS3_MAPWALK)
/* ---- Map test (make MAPWALK=1) ----------------------------------------------
 * Loads every map the way the game does (spmap with the whole client:
 * renderer, cgame, sounds), gets past the mission screen like the player,
 * plays a few seconds from the start and logs memory and frame time per map.
 * A map that does not fit either fails with an error (logged) or hangs lv2:
 * then the last "[walk] ----" line in the log names it.
 *
 * make MAPWALK=1 WALKSECS=90: the long test, for the scripts and cinematics.
 * Every map is played that long with god mode on (the player stands at the
 * start, the AI and the map scripts run: intro cameras, enemies that come
 * looking, scripted events on timers); a cutscene map plays its cameras until
 * its script loads the next level (at most WALK_CUTSCENE_MS). Per map it logs
 * how many cameras started ("[cam]" lines) and how many console lines said
 * warning or error. */
#include "client/client.h"

#define WALK_LOAD_TIMEOUT_MS 120000
#define WALK_PLAY_MS         6000       /* every map */
#define WALK_PLAY_LIST_MS    20000      /* WALKMAPS: the maps asked for */
#define WALK_CUTSCENE_MS     300000     /* WALKSECS: a cutscene map, until it moves on */

static int ps3_walk_play_ms = WALK_PLAY_MS;
static int ps3_walk_long;               /* WALKSECS given */

static void PS3_WalkFrame(u64 *last)
{
    u64 now;

    PS3_Pump();
    PS3_Input_Frame();
    Com_Frame();
    now = PS3_NowUs();
    PS3_ProfFrameEnd((double)(now - *last));
    *last = now;
}

static qboolean PS3_WalkInGame(const char *map)
{
    return clc.state == CA_ACTIVE && com_sv_running->integer &&
           !Q_stricmp(Cvar_VariableString("mapname"), map);
}

/* the mission screen: continue like the player does (uiScript playerstart);
 * in the long test, god mode on (cheats as with spdevmap, without changing
 * how the map loads) */
static int PS3_WalkStart(const char *map, u64 *last)
{
    int t0 = Sys_Milliseconds(), pregame = 0;

    while (ps3_running && PS3_WalkInGame(map) && Sys_Milliseconds() - t0 < 5000) {
        PS3_WalkFrame(last);
        if ((Key_GetCatcher() & KEYCATCH_UI) && uivm &&
            VM_Call(uivm, UI_GET_ACTIVE_MENU) == UIMENU_PREGAME) {
            Cbuf_AddText("fade 0 0 0 0 3\n");
            Cvar_Set("g_playerstart", "1");
            VM_Call(uivm, UI_SET_ACTIVE_MENU, UIMENU_NONE);
            pregame = 1;
            break;
        }
    }
    if (ps3_walk_long)
        Cvar_Set("sv_cheats", "1");     /* "god" goes once the game has seen it */
    return pregame;
}

/* A script loaded the next level by itself (the end of a cutscene map or of
 * a mission): follow it like the player, through the load and the mission
 * screen, and play a few seconds. Until that screen is passed the game keeps
 * g_reloading set and refuses any other map command. */
static void PS3_WalkFollow(const char *map, u64 *last)
{
    int start = Sys_Milliseconds(), t0;

    while (ps3_running && !PS3_WalkInGame(map) &&
           Sys_Milliseconds() - start < WALK_LOAD_TIMEOUT_MS)
        PS3_WalkFrame(last);
    if (!PS3_WalkInGame(map)) {
        PS3_Logf("[walk] -> %s: FAILED to load (client state %d, server map \"%s\")",
                 map, (int)clc.state, Cvar_VariableString("mapname"));
        return;
    }
    PS3_Logf("[walk] -> %s: in game after %.1f s%s", map, (Sys_Milliseconds() - start) / 1000.0,
             PS3_WalkStart(map, last) ? "" : " (no mission screen)");
    t0 = Sys_Milliseconds();
    while (ps3_running && PS3_WalkInGame(map) && Sys_Milliseconds() - t0 < 5000)
        PS3_WalkFrame(last);
    PS3_Logf("[walk] -> %s: %s", map, PS3_WalkInGame(map) ? "plays" : "left while playing");
}

static qboolean PS3_WalkMap(const char *map)
{
    int start = Sys_Milliseconds();
    int t0, frames = 0, slow = 0, pregame, reloads = 0, god = 0;
    double worst = 0, playUs = 0;
    u64 last = PS3_NowUs();

    int playMs = ps3_walk_play_ms;

    PS3_Logf("[walk] ---- %s ----", map);
    PS3_LogMem("[walk] before:");
    ps3_log_warnings = ps3_log_cams = 0;
    Cvar_Set("g_reloading", "0");       /* as the main menu does */
    Cbuf_AddText(va("spmap %s\n", map));

    while (ps3_running && !PS3_WalkInGame(map) &&
           Sys_Milliseconds() - start < WALK_LOAD_TIMEOUT_MS)
        PS3_WalkFrame(&last);
    if (!PS3_WalkInGame(map)) {
        PS3_Logf("[walk] %s: FAILED to load (client state %d, server map \"%s\")",
                 map, (int)clc.state, Cvar_VariableString("mapname"));
        return qfalse;
    }
    PS3_Logf("[walk] %s: in game after %.1f s", map, (Sys_Milliseconds() - start) / 1000.0);

    pregame = PS3_WalkStart(map, &last);
    if (ps3_walk_long && !Q_stricmpn(map, "cutscene", 8) && playMs < WALK_CUTSCENE_MS)
        playMs = WALK_CUTSCENE_MS;

    /* play from the start */
    PS3_ProfReset();
    t0 = Sys_Milliseconds();
    while (ps3_running && Sys_Milliseconds() - t0 < playMs) {
        u64 before = last;
        double dt;

        if (!PS3_WalkInGame(map)) {
            int r0 = Sys_Milliseconds();

            /* the same map again: a failed objective reloads it */
            if (!com_sv_running->integer || Q_stricmp(Cvar_VariableString("mapname"), map) ||
                ++reloads > 3)
                break;
            while (ps3_running && !PS3_WalkInGame(map) &&
                   Sys_Milliseconds() - r0 < WALK_LOAD_TIMEOUT_MS)
                PS3_WalkFrame(&last);
            if (!PS3_WalkInGame(map))
                break;
            PS3_Logf("[walk] %s: the map reloaded itself after %.1f s (%.1f s to load)",
                     map, (r0 - t0) / 1000.0, (Sys_Milliseconds() - r0) / 1000.0);
            PS3_WalkStart(map, &last);
            god = 0;
            continue;
        }
        if (ps3_walk_long && !god && Sys_Milliseconds() - t0 >= 2000) {
            Cbuf_AddText("god\n");
            god = 1;
        }
        PS3_WalkFrame(&last);
        dt = (double)(last - before);
        playUs += dt;
        frames++;
        if (dt > 20000.0)
            slow++;
        if (dt > worst)
            worst = dt;
    }
    if (!PS3_WalkInGame(map)) {
        char now[MAX_QPATH];

        Q_strncpyz(now, Cvar_VariableString("mapname"), sizeof(now));

        if (com_sv_running->integer && now[0] && Q_stricmp(now, map)) {
            /* cutscene maps load the next level by themselves */
            PS3_Logf("[walk] %s: OK, its script moved on to %s after %.1f s, "
                     "%d cameras, %d warning/error lines", map, now,
                     (Sys_Milliseconds() - t0) / 1000.0, ps3_log_cams, ps3_log_warnings);
            PS3_WalkFollow(now, &last);
            PS3_LogMem("[walk] after:");
            return qtrue;
        }
        if (cls.endgamemenu) {
            /* the last cutscene ends the game: credits, then back to the
             * menus; let the menu come up before the next map */
            int e0 = Sys_Milliseconds();

            while (ps3_running && Sys_Milliseconds() - e0 < 5000)
                PS3_WalkFrame(&last);
            PS3_Logf("[walk] %s: OK, the game ended after %.1f s (credits), "
                     "%d cameras, %d warning/error lines", map, (e0 - t0) / 1000.0,
                     ps3_log_cams, ps3_log_warnings);
            PS3_LogMem("[walk] after:");
            return qtrue;
        }
        PS3_Logf("[walk] %s: dropped while playing after %.1f s (client state %d, "
                 "server map \"%s\"), %d warning/error lines", map,
                 (Sys_Milliseconds() - t0) / 1000.0, (int)clc.state, now, ps3_log_warnings);
        PS3_LogMem("[walk] after:");
        return qfalse;
    }
    PS3_Logf("[walk] %s: OK%s, %.1f fps over %.1f s, %d frames over 20 ms, worst %.1f ms",
             map, pregame ? "" : " (no mission screen)",
             playUs > 0 ? frames * 1000000.0 / playUs : 0.0, playUs / 1000000.0,
             slow, worst / 1000.0);
    PS3_Logf("[walk] %s: %d cameras, %d warning/error lines%s", map,
             ps3_log_cams, ps3_log_warnings,
             ps3_walk_long && !Q_stricmpn(map, "cutscene", 8) ? ", STILL in the cutscene" : "");
    PS3_ProfReport();
    PS3_LogMem("[walk] after:");
    return qtrue;
}

static void PS3_MapWalk(void)
{
    char **maps;
    int numMaps, i, ok = 0;
    char failed[1024] = "";
    u64 last = PS3_NowUs();

    for (i = 0; i < 120 && ps3_running; i++)    /* main menu up */
        PS3_WalkFrame(&last);

    if (PS3_WALKMAPS[0]) {
        /* make MAPWALK=1 WALKMAPS="a b c": those maps, played longer */
        static char list[256];
        static char *names[32];
        char *p;

        Q_strncpyz(list, PS3_WALKMAPS, sizeof(list));
        numMaps = 0;
        for (p = strtok(list, " "); p && numMaps < 32; p = strtok(NULL, " "))
            names[numMaps++] = p;
        maps = names;
        ps3_walk_play_ms = WALK_PLAY_LIST_MS;
    } else {
        maps = FS_ListFiles("maps", ".bsp", &numMaps);
    }
    if (PS3_WALKSECS > 0) {
        ps3_walk_long = 1;
        ps3_walk_play_ms = PS3_WALKSECS * 1000;
    }
    PS3_Logf("[walk] map test: %d maps, %d s each%s, hunk %d MB", numMaps,
             ps3_walk_play_ms / 1000, ps3_walk_long ? " (god, cutscenes to the end)" : "",
             Cvar_VariableIntegerValue("com_hunkMegs"));
    for (i = 0; i < numMaps && ps3_running; i++) {
        char map[MAX_QPATH];

        COM_StripExtension(maps[i], map, sizeof(map));
        if (PS3_WalkMap(map))
            ok++;
        else
            Q_strcat(failed, sizeof(failed), va(" %s", map));
    }
    if (!PS3_WALKMAPS[0])
        FS_FreeFileList(maps);
    PS3_Logf("[walk] done: %d of %d maps OK%s%s", ok, numMaps,
             failed[0] ? ", failed:" : "", failed);
    Com_Quit_f();
}
#endif

#if PS3_STAGE == 1

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
    /* spmap like the game (single player gametype): "map" forces FFA, which
     * drops the "notfree" entities and breaks norway's script */
    Cbuf_AddText(va("spmap %s\n", map));

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

#if PS3_STAGE >= 2
    PS3_Input_Init();
    ps3_log("[5] pad initialized");
    PS3_RSX_Init();
    PS3_ExitHook = PS3_Shutdown;
    ps3_log("[6] RSX initialized");
#endif

    Sys_PlatformInit();

#if PS3_STAGE >= 2
    /* Hunk from what is really free once the RSX IO buffer is taken: minus
     * the zone, the sound cache and a margin for the renderer's mallocs. */
    {
        int freeMB = (int)PS3_FreeUserMemMB();
        int soundMB = (PS3_MAX_COMSOUNDMEGS * 1536 * 2064) >> 20;
        int hunkMB = freeMB - PS3_DEF_COMZONEMEGS - soundMB - 24;

        if (hunkMB > 128)
            hunkMB = 128;
        if (hunkMB < PS3_MIN_COMHUNKMEGS)
            hunkMB = PS3_MIN_COMHUNKMEGS;
        PS3_Logf("[6] free ~%d MB -> com_hunkMegs %d (zone %d, sound %d)",
                 freeMB, hunkMB, PS3_DEF_COMZONEMEGS, soundMB);
        Com_sprintf(ps3_hunkarg, sizeof(ps3_hunkarg), "+set com_hunkMegs %d ", hunkMB);
    }
#endif

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
#else
        "%s"
        "+set com_zoneMegs " XSTRING(PS3_DEF_COMZONEMEGS) " "
        "+set com_soundMegs " XSTRING(PS3_MAX_COMSOUNDMEGS) " "
        "+set r_mode -1 "
        "+set r_customwidth %d +set r_customheight %d "
        "+set r_ext_compressed_textures 0 "
        "+set r_primitives 2 "
        "+set r_flares 0 "
        "+set r_drawSun 0 "
        "+set com_maxfps 60 "
#ifdef PS3_DIAG
        "+set cg_drawFPS 1 "        /* on-screen fps while testing */
#endif
#endif
        "+set vm_game 0 +set vm_cgame 0 +set vm_ui 0 "
        "+set com_logfile 0"
        , ps3_hunkarg
#if PS3_STAGE >= 2
        , PS3_RSX_Width(), PS3_RSX_Height()
#endif
        );

    ps3_log("[5] Com_Init");
    Com_Init(ps3_cmdline);
    PS3_Logf("[5] Com_Init done, free user mem ~%u MB", PS3_FreeUserMemMB());
#if PS3_STAGE >= 2 && !defined(PS3_DIAG)
    /* the test builds forced the fps counter on and it went into
     * wolfconfig.cfg: off once (the console can still turn it on) */
    if (Cvar_VariableIntegerValue("ps3_cfgver") < 1) {
        Cvar_Set("cg_drawFPS", "0");
        Cvar_Get("ps3_cfgver", "0", CVAR_ARCHIVE);
        Cvar_Set("ps3_cfgver", "1");
    }
#endif

#if PS3_STAGE == 1
    PS3_Stage1();

    ps3_log("[8] shutdown");
    SV_Shutdown("Server quit");
    Com_Shutdown();
    FS_Shutdown(qtrue);
    ps3_log("[8] shutdown done");
    sysThreadExit(0);
#else
    ps3_log("[7] main loop");
#ifdef PS3_MAPWALK
    PS3_MapWalk();
#endif
    {
        int frames = 0;

        while (ps3_running) {
            PS3_Pump();
            PS3_Input_Frame();
            Com_Frame();
            if (++frames == 3)
                ps3_log("[7] first frames done");
            PS3_FrameStats();
        }
    }

    /* XMB exit: the same path as the Quit menu (Com_Quit_f -> Sys_Quit ->
     * PS3_Shutdown -> exit), config written on the way. */
    ps3_log("[8] XMB exit requested");
    Com_Quit_f();
#endif
}

int main(int argc, char *argv[])
{
    sys_ppu_thread_t tid;
    u64 retval = 0;
    s32 ret;

    (void)argc; (void)argv;

    PS3_LogInit();
    PS3_Logf("[0] main reached: iortcw-ps3 stage %d, patch " PS3_PATCHLEVEL ", built " __DATE__ " " __TIME__,
             PS3_STAGE);

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
