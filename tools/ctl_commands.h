/* ctl_commands.h - Command handlers for preheat-ctl
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Commands are split across multiple files by category:
 *   - ctl_cmd_basic.c  - Daemon lifecycle (status, pause, resume, etc.)
 *   - ctl_cmd_stats.c  - Statistics & monitoring (stats, health, mem)
 *   - ctl_cmd_apps.c   - App management (explain, predict, promote, etc.)
 *   - ctl_cmd_io.c     - Import/export (export, import)
 */

#ifndef CTL_COMMANDS_H
#define CTL_COMMANDS_H

/* === Basic daemon commands (ctl_cmd_basic.c) === */

/* Check daemon running state */
int cmd_status(void);

/* Temporarily disable preloading */
int cmd_pause(const char *duration);

/* Re-enable preloading */
int cmd_resume(void);

/* Reload daemon configuration (SIGHUP) */
int cmd_reload(void);

/* Dump state to log file (SIGUSR1) */
int cmd_dump(void);

/* Save state immediately (SIGUSR2) */
int cmd_save(void);

/* Gracefully stop daemon (SIGTERM) */
int cmd_stop(void);


/* === Statistics commands (ctl_cmd_stats.c) === */

/* Display preload statistics */
int cmd_stats(void);

/* Display detailed statistics with top apps */
int cmd_stats_verbose(void);

/* Quick system health check with exit codes */
int cmd_health(void);

/* Display memory statistics */
int cmd_mem(void);


/* === App management commands (ctl_cmd_apps.c) === */

/* Explain why an app is/isn't preloaded */
int cmd_explain(const char *app_name);

/* Show top predicted applications */
int cmd_predict(int top_n);

/* Add app to priority pool */
int cmd_promote(const char *app_name);

/* Add app to observation pool (blacklist) */
int cmd_demote(const char *app_name);

/* Remove manual override for an app */
int cmd_reset(const char *app_name);

/* Display observation pool apps */
int cmd_show_hidden(void);


/* === Import/export commands (ctl_cmd_io.c) === */

/* Export learned patterns to JSON */
int cmd_export(const char *filepath);

/* Validate JSON import file */
int cmd_import(const char *filepath);

#endif /* CTL_COMMANDS_H */
