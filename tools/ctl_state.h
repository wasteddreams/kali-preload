/* ctl_state.h - State file and path utilities for preheat-ctl
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CTL_STATE_H
#define CTL_STATE_H

#include <glib.h>

/**
 * Convert file:// URI to plain filesystem path
 * 
 * @param uri  URI string (e.g., "file:///usr/bin/firefox")
 * @return Newly allocated string (caller must g_free) or NULL on error
 */
char *uri_to_path(const char *uri);

/**
 * Check if string is a file:// URI
 *
 * @param str  String to check
 * @return TRUE if string starts with "file://"
 */
gboolean is_uri(const char *str);

/**
 * Check if two paths match, handling URIs, basenames, and partial paths
 * 
 * Matching layers (in order):
 *  1. Exact match - fastest path
 *  2. Substring match - handles partial paths
 *  3. Basename match - handles different install locations
 *
 * @param search_path  Path user is searching for (plain path)
 * @param state_path   Path from state file (might be URI)
 * @return TRUE if paths refer to same file
 */
gboolean paths_match(const char *search_path, const char *state_path);

/**
 * Try to resolve app name to full path
 *
 * Searches common binary paths (/usr/bin, /bin, /usr/local/bin) and
 * follows symlinks to find the actual binary.
 *
 * @param name     App name or path (e.g., "firefox" or "/usr/bin/firefox")
 * @param buffer   Output buffer for resolved path (must be PATH_MAX)
 * @param bufsize  Size of buffer
 * @return Resolved path, or original name if not found
 */
const char *resolve_app_name(const char *name, char *buffer, size_t bufsize);

#endif /* CTL_STATE_H */
