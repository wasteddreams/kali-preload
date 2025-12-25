/* pattern.c - Pattern matching implementation for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * =============================================================================
 * MODULE OVERVIEW: Path Pattern Matching
 * =============================================================================
 *
 * Provides glob pattern matching and path prefix operations for the two-tier
 * tracking system. Used to classify applications into priority vs observation
 * pools based on their filesystem paths.
 *
 * PRIMARY USES:
 *   1. EXCLUDED_PATTERNS: Glob matching to exclude system processes
 *      Example: "/usr/lib/ *" matches all libraries
 *
 *   2. USER_APP_PATHS: Directory prefix matching for user applications
 *      Example: "/opt/" matches anything under /opt
 *
 * PATTERN SYNTAX:
 *   - Standard glob wildcards: * (any chars), ? (one char)
 *   - Path-aware: * does NOT match directory separators (/)
 *   - Uses POSIX fnmatch() with FNM_PATHNAME flag
 *
 * BOUNDARY MATCHING:
 *   Directory prefix matching ensures proper boundaries:
 *   - "/opt" matches "/opt/app" ✓
 *   - "/opt" does NOT match "/optical" ✗
 *   This prevents false positives from substring matches.
 *
 * =============================================================================
 */

#include "common.h"
#include "pattern.h"
#include <fnmatch.h>

/**
 * Check if a path matches a glob pattern
 *
 * Uses POSIX fnmatch() for glob-style pattern matching.
 * FNM_PATHNAME flag ensures '*' doesn't match '/' separators,
 * so pattern "/usr/lib/ *" matches "/usr/lib/foo.so" but not "/usr/lib/x/y.so".
 *
 * @param path    File path to test (e.g., "/usr/lib/systemd/systemd")
 * @param pattern Glob pattern (e.g., "/usr/lib/ *")
 * @return        TRUE if path matches pattern, FALSE otherwise
 *
 * EXAMPLES:
 *   kp_pattern_match("/usr/bin/bash", "/usr/bin/ *")     → TRUE
 *   kp_pattern_match("/usr/bin/bash", "*bash")          → TRUE
 *   kp_pattern_match("/usr/local/bin/app", "/usr/bin/ *") → FALSE
 */
gboolean
kp_pattern_match(const char *path, const char *pattern)
{
    if (!path || !pattern)
        return FALSE;

    /* Use POSIX fnmatch for glob patterns */
    return (fnmatch(pattern, path, FNM_PATHNAME) == 0);
}

/**
 * Check if a path matches any pattern in a list
 *
 * Convenience function for testing against multiple patterns.
 * Returns TRUE on first match (short-circuit evaluation).
 *
 * @param path     File path to test
 * @param patterns Array of glob pattern strings
 * @param count    Number of patterns in array
 * @return         TRUE if path matches any pattern, FALSE if no matches
 *
 * USAGE:
 *   Used by two-tier tracking to check excluded_patterns config.
 *   If path matches any exclusion pattern, it goes to observation pool.
 */
gboolean
kp_pattern_matches_any(const char *path, char **patterns, int count)
{
    int i;

    if (!path || !patterns)
        return FALSE;

    for (i = 0; i < count; i++) {
        if (patterns[i] && kp_pattern_match(path, patterns[i])) {
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * Check if a path starts with any of the given directory prefixes
 *
 * Performs directory-boundary-aware prefix matching. Unlike simple strncmp,
 * this ensures we match complete directory names, not arbitrary substrings.
 *
 * @param path     File path to test (e.g., "/opt/myapp/bin/prog")
 * @param prefixes Array of directory prefix strings (e.g., ["/opt/", "/usr/local/"])
 * @param count    Number of prefixes in array
 * @return         TRUE if path starts with any prefix, FALSE otherwise
 *
 * BOUNDARY CHECKING:
 *   After finding a prefix match, verifies the next character is either:
 *   - '\0' (path exactly equals prefix)
 *   - '/'  (path is subdirectory of prefix)
 *
 *   This prevents "/opt" from matching "/optical":
 *     "/optical"[4] = 'i' → not '/' or '\0' → no match ✓
 *     "/opt/app"[4] = '/'  → match ✓
 *
 * USAGE:
 *   Used by user_app_paths config to identify priority pool applications.
 */
gboolean
kp_path_in_directories(const char *path, char **prefixes, int count)
{
    int i;
    size_t prefix_len;

    if (!path || !prefixes)
        return FALSE;

    for (i = 0; i < count; i++) {
        if (!prefixes[i])
            continue;

        prefix_len = strlen(prefixes[i]);

        /* Check if path starts with this prefix */
        if (strncmp(path, prefixes[i], prefix_len) == 0) {
            /* Ensure we're matching directory boundaries, not just prefixes
             * e.g., "/opt" should match "/opt/app" but not "/optical" */
            if (path[prefix_len] == '\0' || path[prefix_len] == '/') {
                return TRUE;
            }
        }
    }

    return FALSE;
}
