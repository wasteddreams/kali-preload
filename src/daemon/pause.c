/* pause.c - Pause control implementation for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * =============================================================================
 * MODULE OVERVIEW: Pause Control
 * =============================================================================
 *
 * Allows users to temporarily disable preloading via preheat-ctl:
 *
 *   preheat-ctl pause 3600   # Pause for 1 hour
 *   preheat-ctl pause        # Pause until reboot
 *   preheat-ctl resume       # Resume immediately
 *
 * STATE PERSISTENCE:
 *   Pause state is stored in /run/preheat.pause which:
 *   - Survives daemon restarts (but not reboots)
 *   - Contains expiry timestamp (0 = until reboot)
 *   - Is readable by preheat-ctl for status queries
 *
 * EXPIRY HANDLING:
 *   kp_pause_is_active() checks if pause has expired and automatically
 *   clears the state, so the daemon resumes preloading seamlessly.
 *
 * USE CASES:
 *   - Heavy I/O operations (large downloads, builds)
 *   - Battery-critical situations on laptops
 *   - Debugging when you want to isolate disk activity
 *
 * =============================================================================
 */

#include "common.h"
#include "pause.h"
#include "../utils/logging.h"

#include <sys/stat.h>

/* Pause state file location */
#define PAUSE_FILE "/run/preheat.pause"

/* Global pause state */
static struct {
    gboolean active;       /* Is pause currently active? */
    time_t expiry;         /* When pause expires (0 = until reboot) */
    gboolean initialized;  /* Has init been called? */
} pause_state = {0};

/**
 * Read pause state from file
 */
static void
load_pause_file(void)
{
    FILE *fp;
    time_t expiry = 0;
    time_t now;

    fp = fopen(PAUSE_FILE, "r");
    if (!fp) {
        /* No pause file = not paused */
        pause_state.active = FALSE;
        pause_state.expiry = -1;
        return;
    }

    if (fscanf(fp, "%ld", &expiry) != 1) {
        g_warning("Invalid pause file format, removing");
        fclose(fp);
        unlink(PAUSE_FILE);
        pause_state.active = FALSE;
        pause_state.expiry = -1;
        return;
    }

    fclose(fp);

    now = time(NULL);

    if (expiry == 0) {
        /* 0 means until reboot */
        pause_state.active = TRUE;
        pause_state.expiry = 0;
        g_message("Pause state loaded: paused until reboot");
    } else if (expiry > now) {
        /* Still valid */
        pause_state.active = TRUE;
        pause_state.expiry = expiry;
        g_message("Pause state loaded: paused for %ld more seconds", expiry - now);
    } else {
        /* Expired */
        g_debug("Pause expired, removing stale pause file");
        unlink(PAUSE_FILE);
        pause_state.active = FALSE;
        pause_state.expiry = -1;
    }
}

/**
 * Write pause state to file
 */
static void
save_pause_file(time_t expiry)
{
    FILE *fp;

    fp = fopen(PAUSE_FILE, "w");
    if (!fp) {
        g_warning("Cannot create pause file %s: %s", PAUSE_FILE, strerror(errno));
        return;
    }

    fprintf(fp, "%ld\n", expiry);
    fclose(fp);

}

/**
 * Initialize pause subsystem
 */
void
kp_pause_init(void)
{
    g_debug("Initializing pause subsystem");
    pause_state.initialized = TRUE;
    load_pause_file();
}

/**
 * Check if preloading is currently paused
 */
gboolean
kp_pause_is_active(void)
{
    time_t now;

    if (!pause_state.initialized) {
        kp_pause_init();
    }

    if (!pause_state.active) {
        return FALSE;
    }

    /* Check if pause has expired */
    if (pause_state.expiry == 0) {
        /* Until reboot - always active */
        return TRUE;
    }

    now = time(NULL);
    if (now >= pause_state.expiry) {
        /* Expired - clear pause */
        g_message("Pause expired, resuming preloading");
        kp_pause_clear();
        return FALSE;
    }

    return TRUE;
}

/**
 * Get remaining pause time in seconds
 */
int
kp_pause_remaining(void)
{
    time_t now;

    if (!pause_state.active) {
        return 0;
    }

    if (pause_state.expiry == 0) {
        return -1;  /* Until reboot */
    }

    now = time(NULL);
    if (now >= pause_state.expiry) {
        return 0;
    }

    return (int)(pause_state.expiry - now);
}

/**
 * Set pause state
 */
void
kp_pause_set(int duration_sec)
{
    time_t expiry;

    if (!pause_state.initialized) {
        kp_pause_init();
    }

    if (duration_sec == 0) {
        /* Until reboot */
        expiry = 0;
        g_message("Preloading paused until reboot");
    } else {
        expiry = time(NULL) + duration_sec;
        g_message("Preloading paused for %d seconds", duration_sec);
    }

    pause_state.active = TRUE;
    pause_state.expiry = expiry;
    save_pause_file(expiry);
}

/**
 * Clear pause state (resume preloading)
 */
void
kp_pause_clear(void)
{
    pause_state.active = FALSE;
    pause_state.expiry = -1;

    if (unlink(PAUSE_FILE) == 0) {
        g_message("Preloading resumed (pause cleared)");
    } else if (errno != ENOENT) {
        g_warning("Cannot remove pause file %s: %s", PAUSE_FILE, strerror(errno));
    }
}

/**
 * Get pause expiry timestamp
 */
time_t
kp_pause_expiry(void)
{
    if (!pause_state.active) {
        return -1;
    }
    return pause_state.expiry;
}

/**
 * Free pause resources
 */
void
kp_pause_free(void)
{
    pause_state.active = FALSE;
    pause_state.expiry = -1;
    pause_state.initialized = FALSE;
}
