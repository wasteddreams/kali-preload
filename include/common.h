/* common.h - Common includes and definitions for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * =============================================================================
 * HEADER OVERVIEW: Common Definitions
 * =============================================================================
 *
 * This header is included by ALL daemon source files via:
 *   #include "common.h"
 *
 * PURPOSE:
 *   Provides a single place for:
 *   1. Autoconf-generated config.h (HAVE_* macros, paths, version)
 *   2. Standard C library includes (stdio, stdlib, string, errno)
 *   3. POSIX includes (unistd, fcntl, sys/types, sys/stat)
 *   4. GLib include (the daemon's core utility library)
 *   5. Compatibility macros for different compilers/platforms
 *
 * COMPILATION:
 *   This header must be found via -I$(top_srcdir)/include in CFLAGS.
 *   The Makefile.am sets this up automatically.
 *
 * DEPENDENCIES:
 *   - GLib 2.0 (glib.h) - Core data structures, memory management
 *   - Autoconf config.h - Build-time configuration
 *
 * =============================================================================
 */

#ifndef COMMON_H
#define COMMON_H

/* Include autoconf-generated config.h if available.
 * This defines PACKAGE, VERSION, HAVE_* macros, and paths. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Standard C library includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* POSIX includes */
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

/* GLib - Core utility library for data structures, event loop, logging */
#include <glib.h>

/*
 * =============================================================================
 * TWO-TIER TRACKING SYSTEM
 * =============================================================================
 *
 * Pool classification for app tracking:
 * - POOL_PRIORITY: User applications shown in stats, actively preloaded
 * - POOL_OBSERVATION: System processes tracked for learning only
 */
typedef enum {
    POOL_PRIORITY,      /* User apps - shown in stats, actively preloaded */
    POOL_OBSERVATION    /* System processes - tracked for Markov learning only */
} pool_type_t;

/*
 * G_GNUC_UNUSED - Suppress "unused parameter" warnings.
 * Used on callback parameters that are required by API but not used.
 * Example: static void callback(gpointer G_GNUC_UNUSED key) { ... }
 */
#ifndef G_GNUC_UNUSED
#define G_GNUC_UNUSED __attribute__((__unused__))
#endif

/*
 * RETSIGTYPE - Return type for signal handlers.
 * Historically varied between void and int; now always void on modern systems.
 */
#ifndef RETSIGTYPE
#define RETSIGTYPE void
#endif

#endif /* COMMON_H */
