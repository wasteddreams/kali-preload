/* ctl_display.c - Display formatting utilities for preheat-ctl
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * =============================================================================
 * MODULE: Display Formatting
 * =============================================================================
 *
 * Provides utilities for formatting output in preheat-ctl:
 *   - Number formatting with commas
 *   - (Future) Progress bars, tables, color codes
 *
 * =============================================================================
 */

#include <stdio.h>
#include <string.h>

#include "ctl_display.h"

/**
 * Format number with commas for readability
 * Example: 1234567 -> "1,234,567"
 */
void
format_number(char *buf, unsigned long num)
{
    char temp[64];
    snprintf(temp, sizeof(temp), "%lu", num);
    
    int len = strlen(temp);
    int pos = 0;
    
    for (int i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) {
            buf[pos++] = ',';
        }
        buf[pos++] = temp[i];
    }
    buf[pos] = '\0';
}
