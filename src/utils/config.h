/**
 * config.h — Configuration parsing for portd.
 * MIT License — see LICENSE
 */
#ifndef PORTD_CONFIG_H
#define PORTD_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "../../include/port_manager.h"

#define CONFIG_MAX_POOLS       16
#define CONFIG_LISTEN_MAX     128

typedef struct {
    uint8_t  id;
    char     name[POOL_NAME_MAX];
    uint16_t range_start;
    uint16_t range_end;
    uint32_t default_lease_sec;
} pool_config_t;

typedef struct {
    char          listen[CONFIG_LISTEN_MAX];
    char          log_level[16];
    bool          use_syslog;
    uint32_t      worker_threads;
    pool_config_t pools[CONFIG_MAX_POOLS];
    int           pool_count;
} daemon_config_t;

int  config_load(const char *path, daemon_config_t *out);
void config_default(daemon_config_t *out);
void config_dump(const daemon_config_t *cfg);

#endif /* PORTD_CONFIG_H */
