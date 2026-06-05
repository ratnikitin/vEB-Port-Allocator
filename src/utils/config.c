/**
 * config.c — Minimal JSON config parser (no external dependencies).
 *
 * Handles the specific subset needed by portd:
 *   - Scalar strings, booleans, integers at top level
 *   - Array of pool objects
 *
 * MIT License — see LICENSE
 */

#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ── Defaults ────────────────────────────────────────────────────────── */

void config_default(daemon_config_t *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->listen, "unix:///var/run/portd.sock",
            CONFIG_LISTEN_MAX - 1);
    strncpy(out->log_level, "info", sizeof(out->log_level) - 1);
    out->use_syslog     = false;
    out->worker_threads = 0; /* auto */
    out->pool_count     = 1;
    out->pools[0].id          = 1;
    out->pools[0].range_start = 1024;
    out->pools[0].range_end   = 65535;
    out->pools[0].default_lease_sec = 0;
    strncpy(out->pools[0].name, "default", POOL_NAME_MAX - 1);
}

/* ── Minimal JSON helpers ────────────────────────────────────────────── */

static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Scan until closing '"', write to out, advance *pp past closing '"'. */
static int scan_string(const char **pp, char *out, size_t out_size)
{
    const char *p = *pp;
    if (*p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        if (i + 1 < out_size) out[i++] = *p;
        if (*p == '\\' && *(p+1)) { p++; } /* skip escape */
        p++;
    }
    if (*p != '"') return -1;
    out[i] = '\0';
    *pp = p + 1;
    return 0;
}

/* Scan a decimal integer, advance *pp past last digit. */
static long scan_long(const char **pp)
{
    char *end;
    long val = strtol(*pp, &end, 10);
    *pp = end;
    return val;
}

/* True if the next non-ws token is "true", false if "false". */
static int scan_bool(const char **pp, bool *out)
{
    const char *p = skip_ws(*pp);
    if (strncmp(p, "true", 4) == 0)  { *out = true;  *pp = p + 4; return 0; }
    if (strncmp(p, "false", 5) == 0) { *out = false; *pp = p + 5; return 0; }
    return -1;
}

/* ── Pool object parser ──────────────────────────────────────────────── */

static int parse_pool_object(const char **pp, pool_config_t *pool)
{
    const char *p = *pp;
    p = skip_ws(p);
    if (*p != '{') return -1;
    p++;

    memset(pool, 0, sizeof(*pool));
    pool->range_start = 1024;
    pool->range_end   = 65535;
    strncpy(pool->name, "pool", POOL_NAME_MAX - 1);

    while (*p) {
        p = skip_ws(p);
        if (*p == '}') { p++; break; }
        if (*p == ',') { p++; continue; }

        /* Read key */
        char key[64] = {0};
        if (scan_string(&p, key, sizeof(key)) != 0) {
            log_error("config: expected string key in pool object");
            return -1;
        }
        p = skip_ws(p);
        if (*p != ':') return -1;
        p++;
        p = skip_ws(p);

        if (strcmp(key, "id") == 0) {
            pool->id = (uint8_t)scan_long(&p);
        } else if (strcmp(key, "name") == 0) {
            scan_string(&p, pool->name, POOL_NAME_MAX);
        } else if (strcmp(key, "range_start") == 0) {
            long v = scan_long(&p);
            pool->range_start = (v < 1 || v > 65535) ? 1024 : (uint16_t)v;
        } else if (strcmp(key, "range_end") == 0) {
            long v = scan_long(&p);
            pool->range_end = (v < 1 || v > 65535) ? 65535 : (uint16_t)v;
        } else if (strcmp(key, "default_lease_sec") == 0) {
            pool->default_lease_sec = (uint32_t)scan_long(&p);
        } else {
            /* skip unknown value */
            while (*p && *p != ',' && *p != '}') p++;
        }
    }
    *pp = p;
    return 0;
}

/* ── Top-level parser ────────────────────────────────────────────────── */

int config_load(const char *path, daemon_config_t *out)
{
    config_default(out);

    FILE *f = fopen(path, "r");
    if (!f) {
        log_error("config: cannot open '%s': %s", path, strerror(errno));
        return -1;
    }

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 65536) {
        log_error("config: file too large or empty: %ld bytes", fsize);
        fclose(f);
        return -1;
    }

    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[nread] = '\0';

    const char *p = skip_ws(buf);
    if (*p != '{') {
        log_error("config: expected '{' at start");
        free(buf); return -1;
    }
    p++;

    out->pool_count = 0;

    while (*p) {
        p = skip_ws(p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }

        char key[64] = {0};
        if (scan_string(&p, key, sizeof(key)) != 0) {
            log_error("config: expected key string");
            free(buf); return -1;
        }
        p = skip_ws(p);
        if (*p != ':') { log_error("config: expected ':' after key '%s'", key); free(buf); return -1; }
        p++;
        p = skip_ws(p);

        if (strcmp(key, "listen") == 0) {
            scan_string(&p, out->listen, CONFIG_LISTEN_MAX);
        } else if (strcmp(key, "log_level") == 0) {
            scan_string(&p, out->log_level, sizeof(out->log_level));
        } else if (strcmp(key, "use_syslog") == 0) {
            scan_bool(&p, &out->use_syslog);
        } else if (strcmp(key, "worker_threads") == 0) {
            out->worker_threads = (uint32_t)scan_long(&p);
        } else if (strcmp(key, "pools") == 0) {
            if (*p != '[') { log_error("config: 'pools' must be array"); free(buf); return -1; }
            p++;
            while (*p) {
                p = skip_ws(p);
                if (*p == ']') { p++; break; }
                if (*p == ',') { p++; continue; }
                if (out->pool_count >= CONFIG_MAX_POOLS) {
                    log_warn("config: too many pools, ignoring rest");
                    break;
                }
                if (parse_pool_object(&p, &out->pools[out->pool_count]) != 0) {
                    log_error("config: failed to parse pool object");
                    free(buf); return -1;
                }
                out->pool_count++;
            }
        } else {
            /* skip unknown top-level value */
            while (*p && *p != '\n' && *p != ',') p++;
        }
    }

    free(buf);
    log_info("config: loaded from '%s' (%d pool(s))", path, out->pool_count);
    return 0;
}

void config_dump(const daemon_config_t *cfg)
{
    log_info("config: listen='%s' log_level='%s' syslog=%s workers=%u",
             cfg->listen, cfg->log_level,
             cfg->use_syslog ? "yes" : "no",
             cfg->worker_threads);
    for (int i = 0; i < cfg->pool_count; i++) {
        const pool_config_t *pc = &cfg->pools[i];
        log_info("config:   pool[%d] id=%u name='%s' range=[%u..%u] lease=%us",
                 i, pc->id, pc->name,
                 pc->range_start, pc->range_end,
                 pc->default_lease_sec);
    }
}
