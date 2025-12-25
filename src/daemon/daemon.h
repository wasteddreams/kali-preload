/* daemon.h - Daemon core functions
 *
 * Based on preload 0.6.4
 * Based on the preload daemon
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef DAEMON_H
#define DAEMON_H

/* Global main loop (accessed by signals.c) */
extern GMainLoop *main_loop;

/**
 * Daemonize the process
 * Forks into background, creates new session, changes to /
 */
void kp_daemonize(void);

/**
 * Run the main event loop
 * Sets up periodic tasks and runs until exit signal
 *
 * @param statefile Path to state file
 */
void kp_daemon_run(const char *statefile);

#endif /* DAEMON_H */
