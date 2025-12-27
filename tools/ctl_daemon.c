/* ctl_daemon.c - Daemon communication utilities for preheat-ctl
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * =============================================================================
 * MODULE: Daemon Communication
 * =============================================================================
 *
 * Provides utilities for interacting with the preheat daemon:
 *   - PID file reading
 *   - Process verification (ensure PID is actually preheat)
 *   - Signal sending with user-friendly error messages
 *   - Fallback to pgrep when PID file is stale
 *
 * =============================================================================
 */

#define _DEFAULT_SOURCE  /* For readlink() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "ctl_daemon.h"

/* File paths for daemon communication */
#define PIDFILE "/var/run/preheat.pid"
#define PACKAGE "preheat"

/**
 * Read daemon PID from PID file (internal, does not print errors)
 *
 * @return  PID from file on success, -1 if not found or error
 */
int
read_pid_file(void)
{
    FILE *f;
    int pid = -1;

    f = fopen(PIDFILE, "r");
    if (!f) {
        return -1;
    }

    if (fscanf(f, "%d", &pid) != 1) {
        fclose(f);
        return -1;
    }

    fclose(f);
    return pid;
}

/**
 * Check if process with given PID is a preheat process
 *
 * Verifies both that the process exists and that it's actually
 * the preheat daemon (not a recycled PID).
 *
 * @param pid  Process ID to check
 * @return     1 if running preheat, 0 if not
 */
int
check_running(int pid)
{
    char proc_path[128];
    char exe_buffer[256];
    ssize_t len;

    /* First check if process exists */
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
    if (access(proc_path, F_OK) != 0) {
        return 0;  /* Process doesn't exist */
    }

    /* Verify it's actually preheat by checking /proc/PID/exe */
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/exe", pid);
    len = readlink(proc_path, exe_buffer, sizeof(exe_buffer) - 1);
    if (len > 0) {
        exe_buffer[len] = '\0';
        /* Check if the executable name contains "preheat" */
        if (strstr(exe_buffer, "preheat") != NULL) {
            return 1;  /* Running and is preheat */
        }
    }

    /* If we can't read exe (permission denied), just check if process exists */
    /* This happens when running without root */
    if (errno == EACCES) {
        return 1;  /* Assume it's preheat if we can't verify */
    }

    return 0;  /* Not preheat */
}

/**
 * Find running preheat daemon using pgrep
 *
 * Fallback when PID file is stale or missing. Uses pgrep to scan
 * for a process named "preheat".
 *
 * @return  PID of running daemon, -1 if not found
 */
int
find_running_daemon(void)
{
    FILE *pf;
    char buf[32];
    int pid = -1;

    pf = popen("pgrep -x preheat 2>/dev/null", "r");
    if (pf) {
        if (fgets(buf, sizeof(buf), pf)) {
            pid = atoi(buf);
        }
        pclose(pf);
    }

    return pid;
}

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
int
get_daemon_pid(int verbose)
{
    int pid;

    /* Try PID file first */
    pid = read_pid_file();
    if (pid > 0 && check_running(pid)) {
        return pid;  /* PID file is valid */
    }

    /* PID file missing or stale, try pgrep */
    pid = find_running_daemon();
    if (pid > 0) {
        /* Found via pgrep - PID file was stale */
        return pid;
    }

    /* Daemon not found */
    if (verbose) {
        fprintf(stderr, "Error: %s is not running\n", PACKAGE);
        fprintf(stderr, "Hint: Start with 'sudo systemctl start preheat'\n");
    }
    return -1;
}

/**
 * Read daemon PID from PID file (legacy wrapper for compatibility)
 *
 * @return  PID of daemon on success, -1 if not found or error
 */
int
read_pid(void)
{
    return get_daemon_pid(1);  /* verbose mode */
}

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
int
send_signal(int pid, int sig, const char *action)
{
    if (kill(pid, sig) < 0) {
        fprintf(stderr, "Error: Failed to send signal to %s (PID %d): %s\n",
                PACKAGE, pid, strerror(errno));
        if (errno == EPERM) {
            fprintf(stderr, "Hint: Try with sudo\n");
        }
        return 1;
    }

    printf("%s: %s\n", PACKAGE, action);
    return 0;
}
