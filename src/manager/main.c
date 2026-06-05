/**
 * main.c — portd daemon entry point.
 *
 * Usage: portd [--config /etc/portd/config.json] [--daemon] [--help]
 *
 * Options:
 *   --config PATH    Load config from PATH (default: /etc/portd/config.json)
 *   --daemon         Fork into background (daemonise)
 *   --log-level LVL  Override log level (debug|info|warn|error)
 *   --help           Print usage
 *
 * MIT License — see LICENSE
 */

#define _GNU_SOURCE
#include "server.h"
#include "../utils/config.h"
#include "../utils/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define DEFAULT_CONFIG "/etc/portd/config.json"

static void print_usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  --config PATH      Config file (default: " DEFAULT_CONFIG ")\n"
        "  --daemon           Run in background\n"
        "  --log-level LEVEL  debug|info|warn|error (default: info)\n"
        "  --help             Show this help\n"
        "\n"
        "portd v1.0.0 — van Emde Boas port allocator daemon\n",
        argv0);
}

static void do_daemonise(void)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS); /* parent exits */

    setsid();

    /* Redirect stdio to /dev/null */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > 2) close(devnull);
    }
}

static log_level_t parse_log_level(const char *s)
{
    if (strcmp(s, "debug") == 0) return LOG_LEVEL_DEBUG;
    if (strcmp(s, "info")  == 0) return LOG_LEVEL_INFO;
    if (strcmp(s, "warn")  == 0) return LOG_LEVEL_WARN;
    if (strcmp(s, "error") == 0) return LOG_LEVEL_ERROR;
    return LOG_LEVEL_INFO;
}

int main(int argc, char *argv[])
{
    const char *config_path  = NULL;
    const char *log_level_s  = NULL;
    int         daemonise    = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--config") == 0 && i+1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--daemon") == 0) {
            daemonise = 1;
        } else if (strcmp(argv[i], "--log-level") == 0 && i+1 < argc) {
            log_level_s = argv[++i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Load config */
    daemon_config_t cfg;
    if (config_path) {
        if (config_load(config_path, &cfg) != 0) {
            fprintf(stderr, "Failed to load config: %s\n", config_path);
            return 1;
        }
    } else {
        /* Try default path, fall back to built-in defaults */
        if (access(DEFAULT_CONFIG, R_OK) == 0) {
            config_load(DEFAULT_CONFIG, &cfg);
        } else {
            config_default(&cfg);
        }
    }

    /* CLI log-level overrides config */
    if (log_level_s) {
        strncpy(cfg.log_level, log_level_s, sizeof(cfg.log_level) - 1);
    }

    /* Init logging */
    log_level_t level = parse_log_level(cfg.log_level);
    log_init(cfg.use_syslog || daemonise, level, "portd");
    config_dump(&cfg);

    if (daemonise) do_daemonise();

    /* Create /var/run/portd directory if needed (ignore errors) */
    mkdir("/var/run/portd", 0755);

    /* Start server */
    if (server_init(&cfg) != 0) {
        log_error("main: server_init failed — exiting");
        return 1;
    }

    log_info("portd started (PID %d)", getpid());
    server_run(&cfg);
    server_shutdown();

    log_info("portd exited cleanly");
    return 0;
}
