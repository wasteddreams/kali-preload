/* daemon.c - Daemon core implementation
 *
 * Based on preload 0.6.4 (VERBATIM daemonize logic)
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * =============================================================================
 * MODULE OVERVIEW: Daemon Core
 * =============================================================================
 *
 * This module handles the low-level daemon lifecycle:
 *
 * DAEMONIZATION (kp_daemonize):
 *   1. fork()     → Child continues, parent exits
 *   2. setsid()   → Become session leader (detach from terminal)
 *   3. umask(007) → Set safe file creation mask
 *   4. chdir("/") → Don't block filesystem unmounts
 *
 * MAIN LOOP (kp_daemon_run):
 *   1. Create PID file (/run/preheat.pid)
 *   2. Check for competing daemons (systemd-readahead, ureadahead, preload)
 *   3. Start state management periodic tasks
 *   4. Run GLib main loop (blocks until exit signal)
 *   5. Cleanup: remove PID file
 *
 * COMPETING DAEMON DETECTION:
 *   Other preload daemons can conflict with preheat. We check for:
 *   - systemd-readahead (Fedora/RHEL)
 *   - ureadahead (Ubuntu)
 *   - preload (original daemon this is based on)
 *
 * =============================================================================
 */

#include "common.h"
#include "daemon.h"
#include "../utils/logging.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>

/*
 * Global main loop instance.
 * Accessed by signals.c to call g_main_loop_quit() on exit signals.
 */
GMainLoop *main_loop = NULL;

/* PID file path - use /run if available, fallback to localstatedir */
#ifndef RUNDIR
#define RUNDIR "/run"
#endif
#define PIDFILE RUNDIR "/" PACKAGE ".pid"

/* Forward declaration for state run function (to be implemented) */
extern void kp_state_run(const char *statefile);

/**
 * Daemonize the process
 * (VERBATIM from upstream preload daemonize)
 */
void
kp_daemonize(void)
{
    switch (fork()) {
        case -1:
            g_error("fork failed, exiting: %s", strerror(errno));
            exit(EXIT_FAILURE);
            break;

        case 0:
            /* child - continue */
            break;

        default:
            /* parent - exit */
            if (getpid() == 1) {
                /* Chain to /sbin/init if we are called as init! */
                execl("/sbin/init", "init", NULL);
                execl("/bin/init", "init", NULL);
            }
            exit(EXIT_SUCCESS);
    }

    /* Disconnect from controlling terminal */
    setsid();

    /* Set safe umask */
    umask(0007);

    /* Change to root directory to not block unmounts */
    (void) chdir("/");

    g_debug("daemonized successfully");
}

/**
 * Write PID file
 * 
 * SECURITY: Uses open() with mode to set permissions atomically,
 * avoiding window where file exists with wrong permissions.
 */
static void
kp_write_pidfile(void)
{
    int fd;
    FILE *f;

    /* Atomically create with correct permissions (no chmod race) */
    fd = open(PIDFILE, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) {
        g_warning("failed to create PID file %s: %s", PIDFILE, strerror(errno));
        return;
    }

    f = fdopen(fd, "w");
    if (!f) {
        g_warning("fdopen failed for PID file: %s", strerror(errno));
        close(fd);
        return;
    }

    fprintf(f, "%d\n", getpid());
    fclose(f);  /* Also closes fd */

    g_debug("PID file created: %s", PIDFILE);
}

/**
 * Remove PID file
 */
static void
kp_remove_pidfile(void)
{
    if (unlink(PIDFILE) < 0) {
        if (errno != ENOENT)
            g_warning("failed to remove PID file %s: %s", PIDFILE, strerror(errno));
    } else {
        g_debug("PID file removed");
    }
}

/**
 * Check for competing preload daemons
 * Logs warnings if conflicts are detected
 */
static void
kp_check_competing_daemons(void)
{
    int conflicts = 0;

    /* Check for systemd-readahead */
    if (access("/run/systemd/readahead/", F_OK) == 0) {
        g_warning("Competing daemon detected: systemd-readahead is active");
        g_warning("  Remedy: Run 'systemctl disable systemd-readahead-collect systemd-readahead-replay'");
        conflicts++;
    }

    /* Check for ureadahead and preload by scanning /proc directly */
    const char *competing_daemons[] = {"ureadahead", "preload", NULL};
    
    for (int i = 0; competing_daemons[i] != NULL; i++) {
        DIR *proc_dir = opendir("/proc");
        if (!proc_dir) continue;
        
        struct dirent *entry;
        while ((entry = readdir(proc_dir))) {
            /* Skip non-numeric entries (not PIDs) */
            if (!isdigit(entry->d_name[0])) continue;
            
            /* Read comm file to get process name */
            char comm_path[PATH_MAX];
            snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", entry->d_name);
            
            FILE *comm_file = fopen(comm_path, "r");
            if (!comm_file) continue;
            
            char comm[256];
            if (fgets(comm, sizeof(comm), comm_file)) {
                /* Strip newline */
                size_t len = strlen(comm);
                if (len > 0 && comm[len-1] == '\n') {
                    comm[len-1] = '\0';
                }
                
                /* Check if it matches */
                if (strcmp(comm, competing_daemons[i]) == 0) {
                    g_warning("Competing daemon detected: %s (PID %s)", 
                             competing_daemons[i], entry->d_name);
                    
                    if (strcmp(competing_daemons[i], "ureadahead") == 0) {
                        g_warning("  Remedy: Run 'systemctl disable ureadahead'");
                    } else if (strcmp(competing_daemons[i], "preload") == 0) {
                        g_warning("  Remedy: Run 'systemctl disable preload' or 'apt remove preload'");
                    }
                    
                    conflicts++;
                    fclose(comm_file);
                    break;  /* Found one instance, move to next daemon */
                }
            }
            fclose(comm_file);
        }
        closedir(proc_dir);
    }

    if (conflicts > 0) {
        g_warning("Found %d competing preload daemon(s). Performance may be affected.", conflicts);
        g_warning("Preheat will continue, but consider disabling conflicting services.");
    }
}

/**
 * Run main event loop
 * (Based on upstream preload main loop structure)
 */
void
kp_daemon_run(const char *statefile)
{
    g_debug("starting main event loop");

    /* Create PID file */
    kp_write_pidfile();

    /* Create main loop */
    main_loop = g_main_loop_new(NULL, FALSE);

    if (!main_loop) {
        g_error("failed to create main loop");
        kp_remove_pidfile();
        return;
    }

    /* Check for competing daemons at startup */
    kp_check_competing_daemons();

    /* Start state management (sets up periodic tasks) */
    kp_state_run(statefile);

    /* Run the loop - blocks until g_main_loop_quit() is called */
    g_main_loop_run(main_loop);

    g_debug("main loop exited");

    /* Cleanup */
    if (main_loop) {
        g_main_loop_unref(main_loop);
        main_loop = NULL;
    }

    /* Remove PID file */
    kp_remove_pidfile();
}
