/* ps3_log.c -- log file in USRDIR, thread-safe, rotated at boot, size-capped.
 *
 * The previous run's log is kept as iortcw-ps3.old.log. Every line is flushed
 * so a hang or a crash still leaves the last line on disk. */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <sys/mutex.h>

#include "ps3_log.h"

#define PS3_LOG_OLD_PATH PS3_USRDIR "/iortcw-ps3.old.log"
#define PS3_LOG_MAX_BYTES (4 * 1024 * 1024)

static FILE *log_file;
static long log_bytes;
static int log_capped;
static sys_mutex_t log_mutex;
static int log_mutex_ok;

static void log_lock(void)   { if (log_mutex_ok) sysMutexLock(log_mutex, 0); }
static void log_unlock(void) { if (log_mutex_ok) sysMutexUnlock(log_mutex); }

void PS3_LogInit(void)
{
    sys_mutex_attr_t attr;

    if (log_file)
        return;

    sysMutexAttrInitialize(attr);
    attr.attr_recursive = SYS_MUTEX_ATTR_RECURSIVE;
    log_mutex_ok = (sysMutexCreate(&log_mutex, &attr) == 0);

    remove(PS3_LOG_OLD_PATH);
    rename(PS3_LOG_PATH, PS3_LOG_OLD_PATH);
    log_file = fopen(PS3_LOG_PATH, "w");
    log_bytes = 0;
    log_capped = 0;
}

void PS3_LogShutdown(void)
{
    log_lock();
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    log_unlock();
}

/* Writes msg as-is; adds a newline only when newline is set and msg lacks one. */
static void log_write(const char *msg, int newline)
{
    size_t len;

    if (!log_file || log_capped)
        return;

    len = strlen(msg);
    if (log_bytes + (long)len > PS3_LOG_MAX_BYTES) {
        fputs("\n[log] size cap reached, logging stopped\n", log_file);
        fflush(log_file);
        log_capped = 1;
        return;
    }
    fwrite(msg, 1, len, log_file);
    log_bytes += (long)len;
    if (newline && (len == 0 || msg[len - 1] != '\n')) {
        fputc('\n', log_file);
        log_bytes++;
    }
    fflush(log_file);
}

/* the map test (MAPWALK) reports these per map */
int ps3_log_warnings, ps3_log_cams;

void PS3_LogRaw(const char *msg)
{
    /* not the two shader-script warnings of the game data on every
     * renderer start (the PC version prints them too) */
    if ((strstr(msg, "WARNING") || strstr(msg, "Warning") ||
         strstr(msg, "ERROR") || strstr(msg, "Error")) && !strstr(msg, "In shader file"))
        ps3_log_warnings++;
    else if (!strncmp(msg, "[cam] start", 11))
        ps3_log_cams++;
    log_lock();
    log_write(msg, 0);
    log_unlock();
}

void ps3_log(const char *msg)
{
    log_lock();
    log_write(msg, 1);
    log_unlock();
}

void PS3_Logf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ps3_log(buf);
}
