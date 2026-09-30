/* ps3_sys.c -- Sys_* / CON_* / mmap / minizip glue for iortcw on PS3.
 *
 * Based on ps3_sys.c from IoQuake3-PS3 (Mayo1970), adapted to the iortcw SP
 * engine: everything lives under PS3_USRDIR, the log goes to ps3_log.c and
 * the three game modules (qagame, cgame, ui) are linked into the EBOOT. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <malloc.h>

#include <ppu-types.h>
#include <sys/memory.h>
#include <sys/mutex.h>
#include <sysutil/sysutil.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "sys/sys_local.h"

#include "ps3_log.h"

/* MAX_FOUND_FILES is defined in upstream sys_unix.c, not in any header. */
#ifndef MAX_FOUND_FILES
#define MAX_FOUND_FILES 0x1000
#endif

/* ---- Paths ---------------------------------------------------------------- */

char *Sys_BinaryPath(void)          { return PS3_USRDIR; }
char *Sys_DefaultInstallPath(void)  { return PS3_USRDIR; }
char *Sys_DefaultAppPath(void)      { return PS3_USRDIR; }
char *Sys_DefaultHomePath(void)     { return PS3_USRDIR; }
char *Sys_Cwd(void)                 { return PS3_USRDIR; }
char *Sys_SteamPath(void)           { return ""; }
char *Sys_GogPath(void)             { return ""; }

qboolean Sys_Mkdir(const char *path)
{
    struct stat st;

    if (mkdir(path, 0777) == 0)
        return qtrue;

    /* PSL1GHT mount points can return EACCES instead of EEXIST. */
    if (errno == EEXIST)
        return qtrue;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return qtrue;

    return qfalse;
}

FILE *Sys_FOpen(const char *ospath, const char *mode)
{
    return fopen(ospath, mode);
}

FILE *Sys_Mkfifo(const char *ospath)
{
    (void)ospath;
    return NULL;
}

/* ---- Time ----------------------------------------------------------------- */

static u64 ps3_time_base = 0;

int Sys_Milliseconds(void)
{
    u64 now;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    now = (u64)tv.tv_sec * 1000000ULL + (u64)tv.tv_usec;
    if (ps3_time_base == 0)
        ps3_time_base = now;
    return (int)((now - ps3_time_base) / 1000ULL);
}

int Sys_FileTime(char *path)
{
    struct stat st;

    if (stat(path, &st) == -1)
        return -1;
    return (int)st.st_mtime;
}

void Sys_Sleep(int msec)
{
    if (msec <= 0)
        return;
    usleep(msec * 1000);
}

/* ---- Misc ----------------------------------------------------------------- */

int Sys_PID(void) { return 1; }
qboolean Sys_PIDIsRunning(int pid) { (void)pid; return qtrue; }
void Sys_InitPIDFile(const char *gamedir) { (void)gamedir; }
void Sys_RemovePIDFile(const char *gamedir) { (void)gamedir; }

const char *Sys_Basename(char *path)
{
    char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

const char *Sys_Dirname(char *path)
{
    static char dir[MAX_OSPATH];
    char *p;

    Q_strncpyz(dir, path, sizeof(dir));
    p = strrchr(dir, '/');
    if (p) {
        *p = '\0';
    } else {
        dir[0] = '.';
        dir[1] = '\0';
    }
    return dir;
}

qboolean Sys_RandomBytes(byte *string, int len)
{
    struct timeval tv;
    unsigned int seed;
    int i;

    gettimeofday(&tv, NULL);
    seed = (unsigned int)(tv.tv_sec ^ tv.tv_usec);
    for (i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        string[i] = (byte)((seed >> 16) & 0xFF);
    }
    return qtrue;
}

/* Local XMB user (works offline) -> PSN nickname (needs sign-in) -> "player".
 * Each sysUtil param needs its own exact buffer size or it returns
 * INVALID_VALUE. */
char *Sys_GetCurrentUser(void)
{
    static char username[SYSUTIL_SYSTEMPARAM_NICKNAME_SIZE];
    static int resolved = 0;

    if (!resolved) {
        resolved = 1;
        username[0] = '\0';
        if (sysUtilGetSystemParamString(SYSUTIL_SYSTEMPARAM_ID_CURRENT_USERNAME,
                username, SYSUTIL_SYSTEMPARAM_CURRENT_USERNAME_SIZE) != 0)
            username[0] = '\0';
        username[SYSUTIL_SYSTEMPARAM_CURRENT_USERNAME_SIZE - 1] = '\0';
        if (username[0] == '\0') {
            if (sysUtilGetSystemParamString(SYSUTIL_SYSTEMPARAM_ID_NICKNAME,
                    username, SYSUTIL_SYSTEMPARAM_NICKNAME_SIZE) != 0)
                username[0] = '\0';
            username[SYSUTIL_SYSTEMPARAM_NICKNAME_SIZE - 1] = '\0';
        }
    }
    if (username[0] == '\0')
        return "player";
    return username;
}

cpuFeatures_t Sys_GetProcessorFeatures(void)
{
    return (cpuFeatures_t)CF_ALTIVEC;
}

int Sys_GetHighQualityCPU(void) { return 1; }

qboolean Sys_LowPhysicalMemory(void) { return qfalse; }

void Sys_SetEnv(const char *name, const char *value)
{
    if (value && *value)
        setenv(name, value, 1);
    else
        unsetenv(name);
}

void Sys_StartProcess(char *cmdline, qboolean doexit)
{
    (void)cmdline;
    if (doexit)
        Cbuf_ExecuteText(EXEC_APPEND, "quit\n");
}

void Sys_OpenURL(char *url, qboolean doexit)
{
    (void)url; (void)doexit;
}

/* ---- Critical sections (used by the engine's threaded code paths) ---------- */

void *Sys_InitializeCriticalSection(void)
{
    sys_mutex_attr_t attr;
    sys_mutex_t *m = malloc(sizeof(*m));

    if (!m)
        return NULL;
    sysMutexAttrInitialize(attr);
    attr.attr_recursive = SYS_MUTEX_ATTR_RECURSIVE;
    if (sysMutexCreate(m, &attr) != 0) {
        free(m);
        return NULL;
    }
    return m;
}

void Sys_EnterCriticalSection(void *ptr)
{
    if (ptr)
        sysMutexLock(*(sys_mutex_t *)ptr, 0);
}

void Sys_LeaveCriticalSection(void *ptr)
{
    if (ptr)
        sysMutexUnlock(*(sys_mutex_t *)ptr);
}

/* ---- File listing --------------------------------------------------------- */

void Sys_ListFilteredFiles(const char *basedir, char *subdirs,
                           char *filter, char **list, int *numfiles)
{
    char search[MAX_OSPATH];
    char newsubdirs[MAX_OSPATH];
    char filename[MAX_OSPATH];
    DIR *fdir;
    struct dirent *d;
    struct stat st;

    if (*numfiles >= MAX_FOUND_FILES - 1)
        return;

    if (*subdirs)
        Com_sprintf(search, sizeof(search), "%s/%s", basedir, subdirs);
    else
        Com_sprintf(search, sizeof(search), "%s", basedir);

    fdir = opendir(search);
    if (!fdir)
        return;

    while ((d = readdir(fdir)) != NULL) {
        Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
        if (stat(filename, &st) == -1)
            continue;

        if (S_ISDIR(st.st_mode)) {
            if (Q_stricmp(d->d_name, ".") && Q_stricmp(d->d_name, "..")) {
                if (*subdirs)
                    Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
                else
                    Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
                Sys_ListFilteredFiles(basedir, newsubdirs, filter, list, numfiles);
            }
        }
        if (*numfiles >= MAX_FOUND_FILES - 1)
            break;
        Com_sprintf(filename, sizeof(filename), "%s/%s", subdirs, d->d_name);
        if (!Com_FilterPath(filter, filename, qfalse))
            continue;
        list[*numfiles] = CopyString(filename);
        (*numfiles)++;
    }
    closedir(fdir);
}

char **Sys_ListFiles(const char *directory, const char *extension,
                     char *filter, int *numfiles, qboolean wantsubs)
{
    struct dirent *d;
    DIR *fdir;
    qboolean dironly = wantsubs;
    int nfiles = 0;
    char **listCopy;
    char *list[MAX_FOUND_FILES];
    int extLen;
    int i;

    if (filter) {
        Sys_ListFilteredFiles(directory, "", filter, list, &nfiles);
        list[nfiles] = NULL;
        *numfiles = nfiles;
        if (!nfiles)
            return NULL;
        listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
        for (i = 0; i < nfiles; i++)
            listCopy[i] = list[i];
        listCopy[i] = NULL;
        return listCopy;
    }

    if (!extension)
        extension = "";
    if (extension[0] == '/' && extension[1] == 0) {
        extension = "";
        dironly = qtrue;
    }
    extLen = strlen(extension);

    fdir = opendir(directory);
    if (!fdir) {
        *numfiles = 0;
        return NULL;
    }

    while ((d = readdir(fdir)) != NULL) {
        char path[MAX_OSPATH];
        struct stat st;

        if (nfiles >= MAX_FOUND_FILES - 1)
            break;

        /* opendir() on PS3 does not fill d_type reliably: stat instead. */
        Com_sprintf(path, sizeof(path), "%s/%s", directory, d->d_name);
        if (stat(path, &st) == -1)
            continue;
        if ((dironly && !S_ISDIR(st.st_mode)) ||
            (!dironly && S_ISDIR(st.st_mode)))
            continue;

        if (*extension) {
            int nameLen = strlen(d->d_name);
            if (nameLen < extLen ||
                Q_stricmp(d->d_name + nameLen - extLen, extension))
                continue;
        }

        list[nfiles++] = CopyString(d->d_name);
    }
    list[nfiles] = NULL;
    closedir(fdir);

    *numfiles = nfiles;
    if (!nfiles)
        return NULL;

    listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
    for (i = 0; i < nfiles; i++)
        listCopy[i] = list[i];
    listCopy[i] = NULL;
    return listCopy;
}

void Sys_FreeFileList(char **list)
{
    int i;

    if (!list)
        return;
    for (i = 0; list[i]; i++)
        Z_Free(list[i]);
    Z_Free(list);
}

/* ---- Console / errors / exit --------------------------------------------- */

void CON_Shutdown(void) {}
void CON_Init(void) {}
char *CON_Input(void) { return NULL; }
void CON_Print(const char *msg) { (void)msg; }
unsigned int CON_LogSize(void) { return 0; }
unsigned int CON_LogWrite(const char *in) { (void)in; return 0; }
unsigned int CON_LogRead(char *out, unsigned int outSize) { (void)out; (void)outSize; return 0; }

char *Sys_ConsoleInput(void) { return NULL; }
char *Sys_GetClipboardData(void) { return NULL; }

void Sys_AnsiColorPrint(const char *msg) { PS3_LogRaw(msg); }

void Sys_Print(const char *msg)
{
    PS3_LogRaw(msg);
}

/* Set by ps3_main.c: stops the process cleanly (RSX, audio, pad) on the way out. */
void (*PS3_ExitHook)(int code) = NULL;

void Sys_ErrorDialog(const char *error)
{
    PS3_Logf("[ERROR] %s", error);
}

dialogResult_t Sys_Dialog(dialogType_t type, const char *message, const char *title)
{
    (void)type;
    PS3_Logf("[dialog] %s: %s", title, message);
    return DR_OK;
}

void Sys_Error(const char *error, ...)
{
    va_list ap;
    char msg[4096];

    va_start(ap, error);
    vsnprintf(msg, sizeof(msg), error, ap);
    va_end(ap);

    PS3_Logf("\n=============================\n[FATAL ERROR] %s\n=============================", msg);
    if (PS3_ExitHook)
        PS3_ExitHook(1);
    PS3_LogShutdown();
    exit(1);
}

void Sys_Quit(void)
{
    ps3_log("Sys_Quit");
    if (PS3_ExitHook)
        PS3_ExitHook(0);
    PS3_LogShutdown();
    exit(0);
}

void Sys_SigHandler(int signal)
{
    (void)signal;
    Sys_Quit();
}

/* ---- Platform init -------------------------------------------------------- */

/* PSL1GHT has no free-memory syscall; create+destroy is the only probe. */
unsigned PS3_FreeUserMemMB(void)
{
    unsigned lo = 1, hi = 256, best = 0;

    while (lo <= hi) {
        unsigned mid = (lo + hi) / 2;
        sys_mem_container_t c;

        if (sysMemContainerCreate(&c, (size_t)mid * 1024u * 1024u) == 0) {
            sysMemContainerDestroy(c);
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

void Sys_SetFloatEnv(void)
{
    /* PPC FPSCR -> round-to-nearest, no exceptions. */
    union { unsigned long long u; double d; } fpscr;
    fpscr.u = 0;
    __asm__ __volatile__("mtfsf 255,%0" :: "f"(fpscr.d));
}

void Sys_PlatformInit(void)
{
    Sys_SetFloatEnv();
    PS3_Logf("[mem] free user memory at Sys_PlatformInit: ~%u MB "
             "(DEF_COMHUNKMEGS=%d, DEF_COMZONEMEGS=%d)",
             PS3_FreeUserMemMB(), PS3_DEF_COMHUNKMEGS, PS3_DEF_COMZONEMEGS);
}

void Sys_PlatformExit(void) {}

void Sys_In_Restart_f(void) {}

void Sys_Init(void)
{
    Cmd_AddCommand("in_restart", Sys_In_Restart_f);
    Cvar_Set("arch", OS_STRING " " ARCH_STRING);
    Cvar_Set("username", Sys_GetCurrentUser());
}

/* Real RSX bring-up happens in ps3_main.c. */
void Sys_GLimpInit(void) {}
void Sys_GLimpSafeInit(void) {}

/* ---- Game modules: linked into the EBOOT ---------------------------------- */
/* Each module is partially linked with ps3_module.ld (brackets its .data and
 * .bss) and has every symbol localized except <mod>_dllEntry/<mod>_vmMain and
 * the brackets (see Makefile). A "dll load" restores the pristine .data and
 * zeroes .bss, like loading a fresh .so would. */

typedef void (*dllEntryProc)(intptr_t (QDECL *syscallptr)(intptr_t, ...));

typedef struct {
    const char *name;
    dllEntryProc dllEntry;
    vmMainProc vmMain;
    char *dataStart, *dataEnd, *bssStart, *bssEnd;
    char *pristine;
} ps3Module_t;

#define PS3_DECLARE_MODULE(mod) \
    extern void mod##_dllEntry(intptr_t (QDECL *syscallptr)(intptr_t, ...)); \
    extern intptr_t mod##_vmMain(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, \
        intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t); \
    extern char __##mod##_data_start[], __##mod##_data_end[]; \
    extern char __##mod##_bss_start[], __##mod##_bss_end[];

#define PS3_MODULE_ENTRY(mod) \
    { #mod, mod##_dllEntry, mod##_vmMain, __##mod##_data_start, __##mod##_data_end, \
      __##mod##_bss_start, __##mod##_bss_end, NULL },

#ifdef PS3_NATIVE_QAGAME
PS3_DECLARE_MODULE(qagame)
#endif
#ifdef PS3_NATIVE_CGAME
PS3_DECLARE_MODULE(cgame)
#endif
#ifdef PS3_NATIVE_UI
PS3_DECLARE_MODULE(ui)
#endif

static ps3Module_t ps3_modules[] = {
#ifdef PS3_NATIVE_QAGAME
    PS3_MODULE_ENTRY(qagame)
#endif
#ifdef PS3_NATIVE_CGAME
    PS3_MODULE_ENTRY(cgame)
#endif
#ifdef PS3_NATIVE_UI
    PS3_MODULE_ENTRY(ui)
#endif
    { NULL }
};

/* FS_FindVM hands us a path ("<dir>/qagame.sp.cell.sprx"); match on the module name. */
static ps3Module_t *PS3_FindModule(const char *name)
{
    char base[MAX_QPATH];
    char *dot;
    ps3Module_t *m;

    Q_strncpyz(base, Sys_Basename((char *)name), sizeof(base));
    dot = strchr(base, '.');
    if (dot)
        *dot = '\0';
    for (m = ps3_modules; m->name; m++) {
        if (!Q_stricmp(base, m->name))
            return m;
    }
    return NULL;
}

qboolean PS3_HasNativeModule(const char *name)
{
    return PS3_FindModule(name) != NULL;
}

void * QDECL Sys_LoadGameDll(const char *name, vmMainProc *entryPoint,
                             intptr_t (QDECL *systemcalls)(intptr_t, ...))
{
    ps3Module_t *m = PS3_FindModule(name);
    size_t dataLen;

    if (!m) {
        PS3_Logf("Sys_LoadGameDll: no linked-in module for '%s'", name);
        return NULL;
    }

    dataLen = (size_t)(m->dataEnd - m->dataStart);
    if (!m->pristine) {
        m->pristine = malloc(dataLen ? dataLen : 1);
        if (!m->pristine)
            return NULL;
        memcpy(m->pristine, m->dataStart, dataLen);
    } else {
        memcpy(m->dataStart, m->pristine, dataLen);
    }
    memset(m->bssStart, 0, (size_t)(m->bssEnd - m->bssStart));

    PS3_Logf("Sys_LoadGameDll: %s (data %u KB, bss %u KB)", m->name,
             (unsigned)(dataLen >> 10), (unsigned)((m->bssEnd - m->bssStart) >> 10));

    m->dllEntry(systemcalls);
    *entryPoint = m->vmMain;
    return m;
}

void Sys_UnloadDll(void *dllHandle)
{
    (void)dllHandle;
}

void *Sys_LoadDll(const char *name, qboolean useSystemLib)
{
    (void)name; (void)useSystemLib;
    return NULL;
}

void *Sys_LoadFunction(void *dllHandle, const char *name)
{
    (void)dllHandle; (void)name;
    return NULL;
}

char *Sys_GetDLLName(const char *name)
{
    return va("%s.sp." ARCH_STRING DLL_EXT, name);
}

qboolean Sys_DllExtension(const char *name)
{
    const char *p;

    if (!name || !*name)
        return qfalse;
    p = strrchr(name, '.');
    return (p && !Q_stricmp(p, DLL_EXT)) ? qtrue : qfalse;
}

/* ---- mmap (no MMU tricks on PS3) ------------------------------------------ */

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    void *p;

    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    p = memalign(16, length);
    if (p)
        memset(p, 0, length);
    return p ? p : (void *)(intptr_t)-1;
}

int munmap(void *addr, size_t length)
{
    (void)length;
    if (addr && addr != (void *)(intptr_t)-1)
        free(addr);
    return 0;
}
