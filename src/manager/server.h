/**
 * server.h — portd server public interface.
 *
 * MIT License — see LICENSE
 */
#ifndef PORTD_SERVER_H
#define PORTD_SERVER_H

#include "../utils/config.h"

/**
 * server_init(cfg) — initialise server from config.
 * Creates listen socket, epoll instance, and all pools.
 * Returns 0 on success, -1 on failure (logs error).
 */
int server_init(const daemon_config_t *cfg);

/**
 * server_run(cfg) — enter event loop. Blocks until SIGTERM/SIGINT.
 */
void server_run(const daemon_config_t *cfg);

/**
 * server_shutdown() — clean up all resources (call after server_run returns).
 */
void server_shutdown(void);

#endif /* PORTD_SERVER_H */
