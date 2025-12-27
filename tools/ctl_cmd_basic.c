/* ctl_cmd_basic.c - Basic daemon lifecycle commands
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Commands: status, pause, resume, reload, dump, save, stop
 */

#define _DEFAULT_SOURCE  /* For usleep() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include "ctl_commands.h"
#include "ctl_daemon.h"
#include "ctl_config.h"

/* File paths */
#define PAUSEFILE "/run/preheat.pause"
#define PACKAGE "preheat"

/**
 * Command: status - Check daemon running state
 */
int
cmd_status(void)
{
    int pid = read_pid();
    if (pid < 0)
        return 1;

    int status = check_running(pid);
    if (status == 1) {
        /* Check pause state */
        FILE *pf = fopen(PAUSEFILE, "r");
        if (pf) {
            long expiry = 0;
            if (fscanf(pf, "%ld", &expiry) == 1) {
                time_t now = time(NULL);
                if (expiry == 0) {
                    printf("%s is running (PID %d) - PAUSED (until reboot)\n", PACKAGE, pid);
                } else if (expiry > now) {
                    int remaining = (int)(expiry - now);
                    int hours = remaining / 3600;
                    int mins = (remaining % 3600) / 60;
                    printf("%s is running (PID %d) - PAUSED (%dh %dm remaining)\n",
                           PACKAGE, pid, hours, mins);
                } else {
                    printf("%s is running (PID %d)\n", PACKAGE, pid);
                }
            } else {
                printf("%s is running (PID %d)\n", PACKAGE, pid);
            }
            fclose(pf);
        } else {
            printf("%s is running (PID %d)\n", PACKAGE, pid);
        }
        return 0;
    } else if (status == 0) {
        fprintf(stderr, "%s is not running (stale PID file?)\n", PACKAGE);
        return 1;
    } else {
        fprintf(stderr, "%s status unknown\n", PACKAGE);
        return 1;
    }
}

/**
 * Command: pause - Temporarily disable preloading
 */
int
cmd_pause(const char *duration)
{
    int seconds = parse_duration(duration);

    if (seconds < 0) {
        fprintf(stderr, "Error: Invalid duration '%s'\n", duration);
        fprintf(stderr, "Examples: 30m, 2h, 1h30m, until-reboot\n");
        return 1;
    }

    FILE *f = fopen(PAUSEFILE, "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot create pause file: %s\n", strerror(errno));
        fprintf(stderr, "Hint: Try with sudo\n");
        return 1;
    }

    time_t expiry = (seconds == 0) ? 0 : time(NULL) + seconds;
    fprintf(f, "%ld\n", expiry);
    fclose(f);

    if (seconds == 0) {
        printf("Preloading paused until reboot\n");
    } else {
        int hours = seconds / 3600;
        int mins = (seconds % 3600) / 60;
        if (hours > 0 && mins > 0) {
            printf("Preloading paused for %dh %dm\n", hours, mins);
        } else if (hours > 0) {
            printf("Preloading paused for %d hour(s)\n", hours);
        } else {
            printf("Preloading paused for %d minute(s)\n", mins);
        }
    }

    return 0;
}

/**
 * Command: resume - Re-enable preloading
 */
int
cmd_resume(void)
{
    if (unlink(PAUSEFILE) == 0) {
        printf("Preloading resumed\n");
        return 0;
    } else if (errno == ENOENT) {
        printf("Preloading was not paused\n");
        return 0;
    } else {
        fprintf(stderr, "Error: Cannot remove pause file: %s\n", strerror(errno));
        fprintf(stderr, "Hint: Try with sudo\n");
        return 1;
    }
}

/**
 * Command: reload - Reload daemon configuration
 */
int
cmd_reload(void)
{
    int pid = read_pid();
    if (pid < 0)
        return 1;

    if (!check_running(pid)) {
        fprintf(stderr, "Error: %s is not running\n", PACKAGE);
        return 1;
    }

    return send_signal(pid, SIGHUP, "configuration reload requested");
}

/**
 * Command: dump - Dump state to log file
 */
int
cmd_dump(void)
{
    int pid = read_pid();
    if (pid < 0)
        return 1;

    if (!check_running(pid)) {
        fprintf(stderr, "Error: %s is not running\n", PACKAGE);
        return 1;
    }

    return send_signal(pid, SIGUSR1, "state dump requested");
}

/**
 * Command: save - Save state immediately
 */
int
cmd_save(void)
{
    int pid = read_pid();
    if (pid < 0)
        return 1;

    if (!check_running(pid)) {
        fprintf(stderr, "Error: %s is not running\n", PACKAGE);
        return 1;
    }

    return send_signal(pid, SIGUSR2, "immediate save requested");
}

/**
 * Command: stop - Gracefully stop daemon
 */
int
cmd_stop(void)
{
    int pid = read_pid();
    if (pid < 0)
        return 1;

    if (!check_running(pid)) {
        fprintf(stderr, "Error: %s is not running\n", PACKAGE);
        return 1;
    }

    int ret = send_signal(pid, SIGTERM, "stop requested");
    if (ret == 0) {
        printf("Waiting for daemon to stop...\n");

        for (int i = 0; i < 50; i++) {
            usleep(100000);  /* 100ms */
            if (!check_running(pid)) {
                printf("%s stopped\n", PACKAGE);
                return 0;
            }
        }

        fprintf(stderr, "Warning: Daemon did not stop after 5 seconds\n");
        return 1;
    }

    return ret;
}
