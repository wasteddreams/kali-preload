/* preheat-ctl.c - CLI control tool for Preheat daemon
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * =============================================================================
 * MODULE OVERVIEW: CLI Control Tool (preheat-ctl)
 * =============================================================================
 *
 * Provides command-line interface for monitoring, controlling, and debugging
 * the preheat daemon. Does NOT link against the daemon - communicates via:
 *   - PID file (/var/run/preheat.pid) for process identification
 *   - Signals (SIGHUP, SIGUSR1, SIGUSR2, SIGTERM) for commands
 *   - Pause file (/run/preheat.pause) for pause state
 *   - Stats file (/run/preheat.stats) for statistics
 *   - State file (preheat.state) for reading learned patterns
 *
 * COMMAND MODULES:
 *   - ctl_cmd_basic.c  - Daemon lifecycle (status, pause, resume, etc.)
 *   - ctl_cmd_stats.c  - Statistics & monitoring (stats, health, mem)
 *   - ctl_cmd_apps.c   - App management (explain, predict, promote, etc.)
 *   - ctl_cmd_io.c     - Import/export (export, import)
 *
 * UTILITY MODULES:
 *   - ctl_daemon.c     - PID file reading, signal sending
 *   - ctl_display.c    - Output formatting
 *   - ctl_state.c      - Path matching utilities
 *   - ctl_config.c     - Config file manipulation
 *
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ctl_commands.h"
#include "ctl_daemon.h"

#define PACKAGE "preheat"
#define DEFAULT_EXPORT "preheat-profile.json"

/**
 * Print usage information and available commands
 */
static void
print_usage(const char *prog)
{
    printf("Usage: %s COMMAND [OPTIONS]\n\n", prog);
    printf("Control the %s daemon\n\n", PACKAGE);
    printf("Commands:\n");
    printf("  status      Check if daemon is running\n");
    printf("  stats       Show preload statistics and hit rate\n");
    printf("  mem         Show memory statistics\n");
    printf("  predict     Show top predicted applications\n");
    printf("  pause       Pause preloading temporarily\n");
    printf("  resume      Resume preloading\n");
    printf("  export      Export learned patterns to JSON file\n");
    printf("  import      Import patterns from JSON file\n");
    printf("  reload      Reload configuration (send SIGHUP)\n");
    printf("  dump        Dump state to log (send SIGUSR1)\n");
    printf("  save        Save state immediately (send SIGUSR2)\n");
    printf("  stop        Stop daemon gracefully (send SIGTERM)\n");
    printf("  update      Update preheat to latest version\n");
    printf("  promote     Add app to priority pool (always show in stats)\n");
    printf("  demote      Add app to observation pool (hide from stats)\n");
    printf("  show-hidden Show apps in observation pool\n");
    printf("  reset       Remove manual override for an app\n");
    printf("  explain     Explain why an app is/isn't preloaded\n");
    printf("  health      Quick system health check (exit codes: 0/1/2)\n");
    printf("  help        Show this help message\n");
    printf("\nOptions for stats:\n");
    printf("  --verbose   Show detailed statistics with top 20 apps\n");
    printf("  -v          Short for --verbose\n");
    printf("\nOptions for predict:\n");
    printf("  --top N     Show top N predictions (default: 10)\n");
    printf("\nOptions for pause:\n");
    printf("  DURATION    Time to pause: 30m, 2h, 1h30m, until-reboot (default: 1h)\n");
    printf("\nOptions for export/import:\n");
    printf("  FILE        Path to JSON file (default: %s)\n", DEFAULT_EXPORT);
    printf("\nOptions for promote/demote/reset/explain:\n");
    printf("  APP         Application name or path (e.g., firefox, /usr/bin/code)\n");
    printf("\n");
}

/**
 * Main entry point - parse command and dispatch
 */
int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Error: No command specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "status") == 0) {
        return cmd_status();
    } else if (strcmp(cmd, "mem") == 0) {
        return cmd_mem();
    } else if (strcmp(cmd, "stats") == 0) {
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
                return cmd_stats_verbose();
            }
        }
        return cmd_stats();
    } else if (strcmp(cmd, "predict") == 0) {
        int top_n = 10;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--top") == 0 && i + 1 < argc) {
                top_n = atoi(argv[i + 1]);
                if (top_n <= 0) top_n = 10;
                i++;
            }
        }
        return cmd_predict(top_n);
    } else if (strcmp(cmd, "reload") == 0) {
        return cmd_reload();
    } else if (strcmp(cmd, "dump") == 0) {
        return cmd_dump();
    } else if (strcmp(cmd, "save") == 0) {
        return cmd_save();
    } else if (strcmp(cmd, "stop") == 0) {
        return cmd_stop();
    } else if (strcmp(cmd, "pause") == 0) {
        const char *duration = (argc > 2) ? argv[2] : NULL;
        return cmd_pause(duration);
    } else if (strcmp(cmd, "resume") == 0) {
        return cmd_resume();
    } else if (strcmp(cmd, "export") == 0) {
        const char *filepath = (argc > 2) ? argv[2] : NULL;
        return cmd_export(filepath);
    } else if (strcmp(cmd, "import") == 0) {
        const char *filepath = (argc > 2) ? argv[2] : NULL;
        return cmd_import(filepath);
    } else if (strcmp(cmd, "update") == 0) {
        if (geteuid() != 0) {
            fprintf(stderr, "Error: Update requires root privileges\n");
            fprintf(stderr, "Try: sudo %s update\n", argv[0]);
            return 1;
        }

        const char *script_locations[] = {
            "/usr/local/share/preheat/update.sh",
            "./scripts/update.sh",
            NULL
        };

        for (int i = 0; script_locations[i] != NULL; i++) {
            if (access(script_locations[i], X_OK) == 0) {
                execl("/bin/bash", "bash", script_locations[i], NULL);
                perror("Failed to execute update script");
                return 1;
            }
        }

        fprintf(stderr, "Error: Update script not found\n");
        fprintf(stderr, "\nManual update procedure:\n");
        fprintf(stderr, "  1. cd /path/to/preheat-linux\n");
        fprintf(stderr, "  2. git pull\n");
        fprintf(stderr, "  3. autoreconf --install --force\n");
        fprintf(stderr, "  4. ./configure\n");
        fprintf(stderr, "  5. make\n");
        fprintf(stderr, "  6. sudo make install\n");
        fprintf(stderr, "  7. sudo systemctl restart preheat\n");
        return 1;
    } else if (strcmp(cmd, "promote") == 0) {
        const char *app_name = (argc > 2) ? argv[2] : NULL;
        return cmd_promote(app_name);
    } else if (strcmp(cmd, "demote") == 0) {
        const char *app_name = (argc > 2) ? argv[2] : NULL;
        return cmd_demote(app_name);
    } else if (strcmp(cmd, "reset") == 0) {
        const char *app_name = (argc > 2) ? argv[2] : NULL;
        return cmd_reset(app_name);
    } else if (strcmp(cmd, "show-hidden") == 0) {
        return cmd_show_hidden();
    } else if (strcmp(cmd, "explain") == 0) {
        const char *app_name = (argc > 2) ? argv[2] : NULL;
        return cmd_explain(app_name);
    } else if (strcmp(cmd, "health") == 0) {
        return cmd_health();
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
