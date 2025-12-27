/* ctl_config.h - Config file manipulation utilities for preheat-ctl
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CTL_CONFIG_H
#define CTL_CONFIG_H

/**
 * Parse duration string like "30m", "2h", "1h30m", "until-reboot"
 *
 * @param str  Duration string to parse
 * @return seconds (positive), 0 for until-reboot, -1 on error
 */
int parse_duration(const char *str);

/**
 * Add entry to configuration file with deduplication
 *
 * Checks if entry already exists before adding.
 *
 * @param filepath  Path to config file
 * @param entry     Entry to add
 * @return          0 on success, 1 on error
 */
int add_to_config_file(const char *filepath, const char *entry);

/**
 * Remove entry from configuration file
 *
 * @param filepath  Path to config file
 * @param entry     Entry to remove
 * @return          0 on success, 1 on error
 */
int remove_from_config_file(const char *filepath, const char *entry);

#endif /* CTL_CONFIG_H */
