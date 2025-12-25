/* blacklist.h - Blacklist support for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef BLACKLIST_H
#define BLACKLIST_H

#include <glib.h>

/**
 * Initialize the blacklist subsystem
 * Loads from /etc/preheat.d/blacklist if exists
 */
void kp_blacklist_init(void);

/**
 * Reload blacklist from file
 * Called on SIGHUP
 */
void kp_blacklist_reload(void);

/**
 * Check if a binary is blacklisted
 * @param binary_name Base name of the binary (not full path)
 * @return TRUE if blacklisted, FALSE otherwise
 */
gboolean kp_blacklist_contains(const char *binary_name);

/**
 * Get count of blacklisted entries
 * @return Number of entries in blacklist
 */
int kp_blacklist_count(void);

/**
 * Free blacklist resources
 */
void kp_blacklist_free(void);

#endif /* BLACKLIST_H */
