/* pattern.h - Pattern matching utilities for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * =============================================================================
 * MODULE OVERVIEW: Pattern Matching for App Classification
 * =============================================================================
 *
 * Provides glob-style pattern matching for two-tier tracking system:
 * - Match paths against exclusion patterns (/bin/STAR, /usr/bin/grep)
 * - Match paths against user app directory prefixes
 * - Support wildcards: STAR (any chars), ? (single char)
 *
 * USAGE EXAMPLE:
 *   char *patterns[] = {"/bin/STAR", "/usr/bin/grep", NULL};
 *   if (kp_pattern_matches_any("/bin/bash", patterns, 2)) {
 *       // Exclude from priority pool
 *   }
 *
 * Note: STAR represents * (asterisk wildcard)
 *
 * =============================================================================
 */

#ifndef PATTERN_H
#define PATTERN_H

#include <glib.h>

/**
 * Check if a path matches a glob pattern
 *
 * Supports glob wildcards and exact matches.
 * See pattern.c implementation for details.
 *
 * @param path Path to test (e.g., "/usr/bin/bash")
 * @param pattern Glob pattern (e.g., "/usr/bin/bash" or with wildcards)
 * @return TRUE if path matches pattern
 */
gboolean kp_pattern_match(const char *path, const char *pattern);

/**
 * Check if a path matches any pattern in a list
 *
 * @param path Path to test
 * @param patterns Array of pattern strings
 * @param count Number of patterns in array
 * @return TRUE if path matches at least one pattern
 */
gboolean kp_pattern_matches_any(const char *path, char **patterns, int count);

/**
 * Check if a path starts with any of the given directory prefixes
 *
 * @param path Path to test
 * @param prefixes Array of directory path prefixes
 * @param count Number of prefixes in array
 * @return TRUE if path starts with any prefix
 */
gboolean kp_path_in_directories(const char *path, char **prefixes, int count);

#endif /* PATTERN_H */
