/**
 * log.h — Simple levelled logging for portd.
 *
 * Logs to stderr (development) or syslog (daemon mode).
 * Level is configured at runtime via log_set_level().
 *
 * MIT License — see LICENSE
 */
#ifndef PORTD_LOG_H
#define PORTD_LOG_H

#include <stdarg.h>
#include <stdbool.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_NONE  = 4,
} log_level_t;

/** Initialise logging. If use_syslog=true, log to syslog; else stderr. */
void log_init(bool use_syslog, log_level_t level, const char *ident);

/** Change log level at runtime. */
void log_set_level(log_level_t level);

/** log_debug / log_info / log_warn / log_error — printf-style macros. */
void _log_write(log_level_t level, const char *file, int line,
                const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#define log_debug(...) _log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...)  _log_write(LOG_LEVEL_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...)  _log_write(LOG_LEVEL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) _log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif /* PORTD_LOG_H */
