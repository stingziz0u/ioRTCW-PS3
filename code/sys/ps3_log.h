#ifndef PS3_LOG_H
#define PS3_LOG_H

void PS3_LogInit(void);
void PS3_LogShutdown(void);
void PS3_LogRaw(const char *msg);            /* engine console text, as-is */
extern int ps3_log_warnings, ps3_log_cams;  /* counted in PS3_LogRaw */
void ps3_log(const char *msg);               /* one line */
void PS3_Logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
