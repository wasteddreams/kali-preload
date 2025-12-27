/* state_map.h - Map and exemap management for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * =============================================================================
 * MODULE: Map and Exemap Management
 * =============================================================================
 *
 * Maps (kp_map_t) represent memory-mapped file regions:
 *
 *   map.path   = "/usr/lib/libc.so.6"
 *   map.offset = 0
 *   map.length = 1847296
 *
 * Maps are shared between executables. When Firefox and Chrome both use
 * libc.so.6, they share the same map object. Reference counting tracks
 * how many executables reference each map.
 *
 * Exemaps (kp_exemap_t) connect executables to the maps they use:
 *
 *   exe ── exemap ──> map
 *             │
 *             └── prob: probability this map is used when exe runs
 *
 * =============================================================================
 */

#ifndef STATE_MAP_H
#define STATE_MAP_H

#include "state.h"

/* Map/exemap management functions are declared in state.h for backward compatibility */

#endif /* STATE_MAP_H */
