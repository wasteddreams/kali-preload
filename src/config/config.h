/* config.h - Configuration handling for Preheat
 *
 * Based on preload 0.6.4 conf.h
 * Based on the preload daemon
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>

/* Unit definitions (for confkeys.h) */
#define bytes			   1
#define kilobytes		1024

#define seconds			   1
#define minutes			  60
#define hours			3600

#define signed_integer_percent	   1
#define percent_times_100	   1  /* Preheat extension */
#define processes		   1

/**
 * Configuration structure
 * (VERBATIM layout from upstream preload_conf_t)
 */
typedef struct _kp_conf_t
{
    /* [model] section - prediction model parameters */
    struct _conf_model {
        int cycle;              /* Scan cycle time (seconds) */
        gboolean usecorrelation; /* Use correlation in predictions */
        
        int minsize;            /* Minimum process size to track (bytes) */
        
        /* Memory usage adjustment percentages */
        int memtotal;           /* % of total memory */
        int memfree;            /* % of free memory */
        int memcached;          /* % of cached memory */
    } model;
    
    /* [system] section - system behavior */
    struct _conf_system {
        gboolean doscan;        /* Enable /proc monitoring */
        gboolean dopredict;     /* Enable predictions and preloading */
        int autosave;           /* State save interval (seconds) */
        
        char **mapprefix;       /* Prefixes for mapped files */
        char **exeprefix;       /* Prefixes for executables */
        
        int maxprocs;           /* Max parallel readahead processes */
        enum {
            SORT_NONE  = 0,     /* No I/O sorting */
            SORT_PATH  = 1,     /* Sort by path */
            SORT_INODE = 2,     /* Sort by inode */
            SORT_BLOCK = 3      /* Sort by disk block */
        } sortstrategy;
        
        char *manualapps;           /* Path to manual apps whitelist file */
        char **manual_apps_loaded;  /* Loaded app paths (runtime) */
        int manual_apps_count;      /* Number of loaded apps */
    } system;
    
#ifdef ENABLE_PREHEAT_EXTENSIONS
    /* [preheat] section - Preheat extensions */
    struct _conf_preheat {
        gboolean enable_preheat_scoring;  /* Enable Preheat tool boosting */
        int preheat_tool_boost;            /* Priority boost (100 = 1.0x, 150 = 1.5x) */
        gboolean enable_time_learning;  /* Enable time-of-day patterns */
        
        char *manual_apps_list;         /* Path to manual apps file */
        char *blacklist;                /* Path to blacklist file */
    } preheat;
#endif
    
} kp_conf_t;

/* Global configuration (singleton, like upstream) */
extern kp_conf_t kp_conf[1];

/**
 * Load configuration from file
 * (VERBATIM signature from upstream preload_conf_load)
 * 
 * @param conffile Path to configuration file
 * @param fail If TRUE, exit on error; if FALSE, warn and continue
 */
void kp_config_load(const char *conffile, gboolean fail);

/**
 * Dump loaded configuration to log
 * (VERBATIM signature from upstream preload_conf_dump_log)
 */
void kp_config_dump_log(void);

#endif /* CONFIG_H */
