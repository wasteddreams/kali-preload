/* state_markov.h - Markov chain management for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * =============================================================================
 * MODULE: Markov Chain Management
 * =============================================================================
 *
 * Markov chains (kp_markov_t) model correlations between application pairs:
 *
 *   exe_a <───── markov ─────> exe_b
 *
 * They track a 4-state continuous-time Markov model:
 *
 *   State │ A running │ B running │ Description
 *   ──────┼───────────┼───────────┼─────────────────────
 *     0   │    No     │    No     │ Neither running
 *     1   │   Yes     │    No     │ Only A running
 *     2   │    No     │   Yes     │ Only B running
 *     3   │   Yes     │   Yes     │ Both running
 *
 * For each state, we track:
 *   - time_to_leave[s]: Mean time spent in state s before transitioning
 *   - weight[i][j]: Count of transitions from state i to state j
 *
 * The correlation() function computes Pearson correlation coefficient
 * from these statistics, determining how "related" two apps are.
 * High correlation → if A is running, B is likely to run soon.
 *
 * =============================================================================
 */

#ifndef STATE_MARKOV_H
#define STATE_MARKOV_H

#include "state.h"

/* Markov management functions are declared in state.h for backward compatibility */

#endif /* STATE_MARKOV_H */
