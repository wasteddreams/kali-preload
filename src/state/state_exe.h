/* state_exe.h - Executable management for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * =============================================================================
 * MODULE: Executable Management
 * =============================================================================
 *
 * Executables (kp_exe_t) represent tracked applications:
 *
 *   exe.path     = "/usr/bin/firefox"
 *   exe.time     = total seconds ever running (for frequency weighting)
 *   exe.exemaps  = set of memory maps this exe uses
 *   exe.markovs  = set of correlations with other exes
 *
 * EXE LIFECYCLE:
 *   1. Discovered via /proc scan → kp_exe_new()
 *   2. Registered in global state → kp_state_register_exe()
 *   3. Markov chains created to all existing exes
 *   4. Time/prob updated each scan cycle
 *   5. Serialized to state file on save
 *
 * The 'seq' field provides stable ordering across save/load cycles
 * since hash table iteration order is not deterministic.
 *
 * =============================================================================
 */

#ifndef STATE_EXE_H
#define STATE_EXE_H

#include "state.h"

/* Exe management functions are declared in state.h for backward compatibility */

#endif /* STATE_EXE_H */
