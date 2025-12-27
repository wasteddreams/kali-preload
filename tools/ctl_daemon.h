/* ctl_daemon.h - Daemon communication utilities for preheat-ctl
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CTL_DAEMON_H
#define CTL_DAEMON_H

/**
 * Read daemon PID from PID file (internal, does not print errors)
 *
 * @return  PID from file on success, -1 if not found or error
 */
int read_pid_file(void);

/**
 * Check if process with given PID is a preheat process
 *
 * Verifies both that the process exists and that it's actually
 * the preheat daemon (not a recycled PID).
 *
 * @param pid  Process ID to check
 * @return     1 if running preheat, 0 if not
 */
int check_running(int pid);

/**
 * Find running preheat daemon using pgrep
 *
 * Fallback when PID file is stale or missing. Uses pgrep to scan
 * for a process named "preheat".
 *
 * @return  PID of running daemon, -1 if not found
 */
int find_running_daemon(void);

/**
 * Get daemon PID with fallback to process scanning
 *
 * First tries the PID file, then falls back to pgrep if:
 *   - PID file doesn't exist
 *   - PID file contains a stale PID
 *
 * @param verbose  If true, print error messages
 * @return         PID of daemon on success, -1 if not found
 */
int get_daemon_pid(int verbose);

/**
 * Read daemon PID from PID file (legacy wrapper for compatibility)
 *
 * @return  PID of daemon on success, -1 if not found or error
 */
int read_pid(void);

/**
 * Send a signal to the daemon process
 *
 * Wrapper around kill() with user-friendly error messages and
 * permission hints.
 *
 * @param pid     Process ID of daemon
 * @param sig     Signal number (SIGHUP, SIGUSR1, etc.)
 * @param action  Human-readable description for success message
 * @return        0 on success, 1 on error
 */
int send_signal(int pid, int sig, const char *action);

#endif /* CTL_DAEMON_H */
