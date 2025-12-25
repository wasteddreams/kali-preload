/* pause.h - Pause control for Preheat daemon
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef PAUSE_H
#define PAUSE_H

#include <glib.h>
#include <time.h>

/**
 * Initialize pause subsystem
 * Checks for existing pause state file
 */
void kp_pause_init(void);

/**
 * Check if preloading is currently paused
 * @return TRUE if paused, FALSE otherwise
 */
gboolean kp_pause_is_active(void);

/**
 * Get remaining pause time in seconds
 * @return Seconds remaining, 0 if not paused, -1 if until-reboot
 */
int kp_pause_remaining(void);

/**
 * Set pause state
 * @param duration_sec Duration in seconds (0 = until reboot)
 */
void kp_pause_set(int duration_sec);

/**
 * Clear pause state (resume preloading)
 */
void kp_pause_clear(void);

/**
 * Get pause expiry timestamp
 * @return Unix timestamp when pause ends, 0 if until-reboot, -1 if not paused
 */
time_t kp_pause_expiry(void);

/**
 * Free pause resources
 */
void kp_pause_free(void);

#endif /* PAUSE_H */
