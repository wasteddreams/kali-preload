/* state_family.h - Application family management for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * =============================================================================
 * MODULE: Application Families (Enhancement #3)
 * =============================================================================
 *
 * Application families group related executables for better stat aggregation:
 *
 *   firefox-family: /usr/bin/firefox + /usr/bin/firefox-esr
 *   vscode-family:  /usr/bin/code + /usr/bin/code-insiders
 *
 * DISCOVERY METHODS:
 *   - CONFIG: User-defined in preheat.conf
 *   - AUTO: Detected via naming patterns (app-beta, app-dev, etc.)
 *   - MANUAL: Created via CLI command
 *
 * This module splits family management from the main state.c file for
 * better code organization and maintainability.
 *
 * =============================================================================
 */

#ifndef STATE_FAMILY_H
#define STATE_FAMILY_H

#include "state.h"

/* Family management functions are declared in state.h for backward compatibility */

#endif /* STATE_FAMILY_H */
