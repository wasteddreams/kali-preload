/* main.c - Preheat daemon entry point
 *
 * Based on the preload daemon
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "common.h"
#include "../utils/logging.h"
#include "../config/config.h"
#include "daemon.h"
#include "signals.h"

#include <getopt.h>

/* Default file paths */
#define DEFAULT_CONFFILE SYSCONFDIR "/" PACKAGE ".conf"
#define DEFAULT_STATEFILE PKGLOCALSTATEDIR "/" PACKAGE ".state"
#define DEFAULT_LOGFILE LOGDIR "/" PACKAGE ".log"
#define DEFAULT_NICELEVEL 15

/* Global variables (accessed by other modules) */
const char *conffile = DEFAULT_CONFFILE;
const char *statefile = DEFAULT_STATEFILE;
const char *logfile = DEFAULT_LOGFILE;
int nicelevel = DEFAULT_NICELEVEL;
int foreground = 0;

/* Forward declarations for functions to be implemented */
extern void kp_config_load(const char *conffile, gboolean is_startup);
extern void kp_state_load(const char *statefile);
extern void kp_state_save(const char *statefile);
extern void kp_state_free(void);

static void
print_version(void)
{
    printf("%s %s\n", PACKAGE, VERSION);
    printf("Adaptive readahead daemon for Debian-based distributions\n");
    printf("Based on the preload daemon\n\n");
    printf("Copyright (C) 2025 Preheat Contributors\n");
    printf("This is free software; see the source for copying conditions.\n");
}

static void
print_help(void)
{
    printf("Usage: %s [OPTIONS]\n\n", PACKAGE);
    printf("Adaptive readahead daemon for Debian-based distributions\n\n");
    printf("Options:\n");
    printf("  -c, --conffile FILE    Configuration file (default: %s)\n", DEFAULT_CONFFILE);
    printf("  -s, --statefile FILE   State file (default: %s)\n", DEFAULT_STATEFILE);
    printf("  -l, --logfile FILE     Log file (default: %s)\n", DEFAULT_LOGFILE);
    printf("  -n, --nice LEVEL       Nice level (default: %d)\n", DEFAULT_NICELEVEL);
    printf("  -f, --foreground       Run in foreground (don't daemonize)\n");
    printf("  -h, --help             Show this help message\n");
    printf("  -v, --version          Show version information\n");
    printf("\n");
    printf("Signals:\n");
    printf("  SIGHUP                 Reload configuration and reopen log\n");
    printf("  SIGUSR1                Dump current state to log\n");
    printf("  SIGUSR2                Save state immediately\n");
    printf("  SIGTERM, SIGINT        Graceful shutdown\n");
    printf("\n");
    printf("Report bugs to: https://github.com/wasteddreams/preheat/issues\n");
}

static void
parse_cmdline(int *argc, char ***argv)
{
    static struct option long_options[] = {
        {"conffile",   required_argument, NULL, 'c'},
        {"statefile",  required_argument, NULL, 's'},
        {"logfile",    required_argument, NULL, 'l'},
        {"nice",       required_argument, NULL, 'n'},
        {"foreground", no_argument,       NULL, 'f'},
        {"help",       no_argument,       NULL, 'h'},
        {"version",    no_argument,       NULL, 'v'},
        {NULL,         0,                 NULL,  0 }
    };

    int c;
    while ((c = getopt_long(*argc, *argv, "c:s:l:n:fhv", long_options, NULL)) != -1) {
        switch (c) {
            case 'c':
                conffile = optarg;
                break;
            case 's':
                statefile = optarg;
                break;
            case 'l':
                logfile = optarg;
                break;
            case 'n':
                nicelevel = atoi(optarg);
                break;
            case 'f':
                foreground = 1;
                break;
            case 'h':
                print_help();
                exit(EXIT_SUCCESS);
            case 'v':
                print_version();
                exit(EXIT_SUCCESS);
            default:
                fprintf(stderr, "Try '%s --help' for more information.\n", PACKAGE);
                exit(EXIT_FAILURE);
        }
    }
}

/**
 * Main entry point
 * (Structure from upstream preload main)
 */
int
main(int argc, char **argv)
{
    /* Initialize */
    parse_cmdline(&argc, &argv);
    kp_log_init(logfile);
    
    /* Load configuration */
    kp_config_load(conffile, TRUE);
    
    kp_signals_init();
    
    if (!foreground)
        kp_daemonize();
    
    if (0 > nice(nicelevel))
        g_warning("nice: %s", strerror(errno));
    
    g_debug("starting up");
    
    /* Load state from file */
    kp_state_load(statefile);
    
    g_message("%s %s started", PACKAGE, VERSION);
    
    /* Main loop */
    kp_daemon_run(statefile);
    
    /* Clean up */
    kp_state_save(statefile);
    kp_state_free();
    
    g_debug("exiting");
    return EXIT_SUCCESS;
}
