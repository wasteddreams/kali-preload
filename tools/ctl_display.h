/* ctl_display.h - Display formatting utilities for preheat-ctl
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CTL_DISPLAY_H
#define CTL_DISPLAY_H

/**
 * Format number with commas for readability
 * Example: 1234567 -> "1,234,567"
 *
 * @param buf  Output buffer (must be large enough for formatted number)
 * @param num  Number to format
 */
void format_number(char *buf, unsigned long num);

#endif /* CTL_DISPLAY_H */
