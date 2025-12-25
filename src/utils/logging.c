/* logging.c - Preheat logging implementation
 *
 * Based on preload 0.6.4 log.c
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * =============================================================================
 * MODULE OVERVIEW: Logging System
 * =============================================================================
 *
 * This module provides daemon-appropriate logging with these features:
 *
 *   - Timestamps on all log messages
 *   - Configurable log level (verbosity)
 *   - File descriptor redirection (stdout/stderr -> logfile)
 *   - Log rotation support via SIGHUP
 *   - Integration with GLib's logging infrastructure
 *
 * LOG LEVELS (from least to most verbose):
 *   0 = Silent (no output)
 *   1 = Errors only
 *   2 = + Warnings  
 *   3 = + Messages (g_message)
 *   4 = Standard (default) - includes g_message and above
 *   5+ = Debug messages (g_debug)
 *
 * DAEMON MODE:
 *   When running as daemon, kp_log_init() redirects stdout/stderr to the
 *   log file and stdin to /dev/null. This ensures all output goes to the
 *   log file even from libraries that write directly to stderr.
 *
 * LOG ROTATION:
 *   On SIGHUP, kp_log_reopen() closes and reopens the log file. This
 *   allows logrotate to rename the old log and create a new one.
 *
 * =============================================================================
 */

#include "common.h"
#include "logging.h"

#include <time.h>

/*
 * Default log level.
 * 4 = Standard messages (G_LOG_LEVEL_MESSAGE and above)
 * Can be increased via command line for debugging.
 */
#define DEFAULT_LOGLEVEL 4

/* Global log level, accessible by other modules via extern in logging.h */
int kp_log_level = DEFAULT_LOGLEVEL;

/**
 * GLib log handler callback
 *
 * This function is installed as the default GLib log handler. It:
 *   1. Filters messages based on kp_log_level
 *   2. Formats messages with timestamp
 *   3. Handles fatal errors by flushing and exiting
 *
 * LOG FILTERING LOGIC:
 *   GLib log levels are bit flags: ERROR=4, CRITICAL=8, WARNING=16, MESSAGE=32, etc.
 *   The expression "G_LOG_LEVEL_ERROR << kp_log_level" creates a threshold.
 *   Higher kp_log_level values allow more verbose messages through.
 *
 * @param log_domain  GLib log domain (usually "preheat" or NULL)
 * @param log_level   Severity level (ERROR, WARNING, MESSAGE, DEBUG, etc.)
 * @param message     The log message text
 * @param user_data   Unused (required by GLib callback signature)
 */
static void
kp_log_handler(const char *log_domain,
               GLogLevelFlags log_level,
               const char *message,
               gpointer G_GNUC_UNUSED user_data)
{
    time_t curtime;
    char *timestr;

    /* Ignore unimportant messages (upstream logic) */
    if (log_level <= G_LOG_LEVEL_ERROR << kp_log_level) {
        curtime = time(NULL);
        timestr = ctime(&curtime);
        timestr[strlen(timestr) - 1] = '\0';  /* Remove trailing newline */

        fprintf(stderr, "[%s] %s%s%s\n",
                timestr,
                log_domain ? log_domain : "",
                log_domain ? ": " : "",
                message);
    }

    /* Handle fatal errors (upstream logic) */
    if (log_level & G_LOG_FLAG_FATAL) {
        kp_log_handler(log_domain, 0, "Exiting", NULL);
        fflush(stdout);
        fflush(stderr);
        exit(EXIT_FAILURE);
    }
}

/**
 * Initialize logging system
 *
 * Sets up file-based logging appropriate for a daemon:
 *   - Redirects stdin to /dev/null (daemons shouldn't read from terminal)
 *   - Redirects stdout and stderr to the log file
 *   - Installs our custom log handler as GLib's default
 *
 * This redirection approach ensures that ALL output goes to the log file,
 * even from third-party libraries that write directly to stderr.
 *
 * @param logfile Path to log file. If NULL or empty, logs go to original stderr.
 *
 * USES:
 *   - Called once at daemon startup after daemonizing
 *   - Fatal error if file cannot be opened (appropriate for startup)
 */
void
kp_log_init(const char *logfile)
{
    if (logfile && *logfile) {
        int logfd;
        int nullfd;

        /* Set up stdout, stderr to log and stdin to /dev/null */

        if (0 > (nullfd = open("/dev/null", O_RDONLY)))
            g_error("cannot open %s: %s", "/dev/null", strerror(errno));

        if (0 > (logfd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, 0600)))
            g_error("cannot open %s: %s", logfile, strerror(errno));

        if ((dup2(nullfd, STDIN_FILENO) != STDIN_FILENO) ||
            (dup2(logfd, STDOUT_FILENO) != STDOUT_FILENO) ||
            (dup2(logfd, STDERR_FILENO) != STDERR_FILENO))
            g_error("dup2: %s", strerror(errno));

        close(nullfd);
        close(logfd);
    }

    /* Install our custom log handler */
    g_log_set_default_handler(kp_log_handler, NULL);
}

/**
 * Reopen log file after rotation
 *
 * Called from SIGHUP handler to support log rotation:
 *   1. logrotate renames /var/log/preheat.log to .log.1
 *   2. logrotate sends SIGHUP to preheat daemon
 *   3. This function opens /var/log/preheat.log (new file)
 *   4. Old file descriptor is replaced with new one
 *
 * This approach is standard for Unix daemons and integrates with
 * the system's logrotate infrastructure without any special configuration.
 *
 * @param logfile Path to log file (same as passed to kp_log_init)
 *
 * THREAD SAFETY:
 *   Not thread-safe (uses dup2). Should only be called from signal handler
 *   or main thread when no other threads are actively logging.
 */
void
kp_log_reopen(const char *logfile)
{
    int logfd;

    if (!(logfile && *logfile))
        return;

    g_message("reopening log file %s", logfile);

    fflush(stdout);
    fflush(stderr);

    if (0 > (logfd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, 0600))) {
        g_warning("cannot reopen %s: %s", logfile, strerror(errno));
        return;
    }

    if ((dup2(logfd, STDOUT_FILENO) != STDOUT_FILENO) ||
        (dup2(logfd, STDERR_FILENO) != STDERR_FILENO))
        g_warning("dup2: %s", strerror(errno));

    close(logfd);

    g_message("reopening log file %s done", logfile);
}
