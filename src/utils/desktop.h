/* desktop.h - Desktop file scanning for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * =============================================================================
 * MODULE OVERVIEW: Desktop File Scanner
 * =============================================================================
 *
 * Scans for .desktop files in standard XDG directories to auto-discover
 * GUI applications that should be in the priority pool.
 *
 * SCANNED DIRECTORIES:
 * - /usr/share/applications
 * - /usr/local/share/applications
 * - ~/.local/share/applications
 *
 * PURPOSE:
 * Auto-promote GUI applications to priority pool without manual configuration.
 * Firefox, VS Code, etc. are automatically recognized and prioritized.
 *
 * USAGE:
 *   kp_desktop_init();  // Call once at startup
 *   if (kp_desktop_has_file("/usr/bin/firefox")) {
 *       // App has .desktop file → priority pool
 *   }
 *   kp_desktop_free();  // Call at shutdown
 *
 * =============================================================================
 */

#ifndef DESKTOP_H
#define DESKTOP_H

#include <glib.h>

/**
 * Initialize desktop file scanner
 * Scans standard directories and builds app registry
 */
void kp_desktop_init(void);

/**
 * Check if an executable has a .desktop file
 *
 * @param exe_path Full path to executable (e.g., "/usr/bin/firefox")
 * @return TRUE if app has a .desktop file (should be in priority pool)
 */
gboolean kp_desktop_has_file(const char *exe_path);

/**
 * Get application name from .desktop file
 *
 * @param exe_path Full path to executable
 * @return Application display name, or NULL if not found
 */
const char *kp_desktop_get_name(const char *exe_path);

/**
 * Free desktop scanner resources
 */
void kp_desktop_free(void);

#endif /* DESKTOP_H */
