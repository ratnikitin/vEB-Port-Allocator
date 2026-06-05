/**
 * log.c — Logging implementation.
 * MIT License — see LICENSE
 */

#define _GNU_SOURCE
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include <pthread.h>

static struct {
    bool        use_syslog;
    log_level_t level;
    char        ident[64];
    pthread_mutex_t lock;
} g_log = {
    .use_syslog = false,
    .level      = LOG_LEVEL_INFO,
    .ident      = "portd",
    .lock       = PTHREAD_MUTEX_INITIALIZER,
};

static const char * const level_str[] = {
    "DEBUG", "INFO ", "WARN ", "ERROR"
};

static int to_syslog_prio[] = {
    LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR
};

void log_init(bool use_syslog, log_level_t level, const char *ident)
{
    pthread_mutex_lock(&g_log.lock);
    g_log.use_syslog = use_syslog;
    g_log.level = level;
    if (ident) {
        strncpy(g_log.ident, ident, sizeof(g_log.ident) - 1);
        g_log.ident[sizeof(g_log.ident)-1] = '\0';
    }
    if (use_syslog) {
        openlog(g_log.ident, LOG_PID | LOG_NDELAY, LOG_DAEMON);
    }
    pthread_mutex_unlock(&g_log.lock);
}

void log_set_level(log_level_t level)
{
    __atomic_store_n(&g_log.level, level, __ATOMIC_RELAXED);
}

void _log_write(log_level_t level, const char *file, int line,
                const char *fmt, ...)
{
    if (level < g_log.level) return;

    va_list ap;
    va_start(ap, fmt);

    if (g_log.use_syslog) {
        vsyslog(to_syslog_prio[level], fmt, ap);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm tm_info;
        localtime_r(&ts.tv_sec, &tm_info);

        char timebuf[32];
        strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_info);

        const char *basename = strrchr(file, '/');
        basename = basename ? basename + 1 : file;

        pthread_mutex_lock(&g_log.lock);
        fprintf(stderr, "%s.%03ld [%s] %s:%d — ",
                timebuf, ts.tv_nsec / 1000000L,
                level_str[level], basename, line);
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
        pthread_mutex_unlock(&g_log.lock);
    }

    va_end(ap);
}
