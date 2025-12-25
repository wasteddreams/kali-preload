/* stats.h - Statistics tracking for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef STATS_H
#define STATS_H

#include <glib.h>
#include <time.h>

/* Maximum apps to track in top list (Enhancement #5: increased from 5 to 20) */
#define STATS_TOP_APPS 20

/* Statistics summary structure */
typedef struct _kp_stats_summary {
    /* Counters */
    unsigned long preloads_total;
    unsigned long preload_hits;
    unsigned long preload_misses;

    /* Derived */
    double hit_rate;

    /* Current state */
    int apps_tracked;
    int apps_preloaded;
    time_t daemon_start;
    time_t last_prediction;

    /* Enhancement #5: Pool breakdown */
    int priority_pool_count;
    int observation_pool_count;

    /* Enhancement #5: Memory metrics */
    size_t total_preloaded_bytes;
    unsigned long memory_pressure_events;

    /* Top apps */
    struct {
        char *name;
        unsigned long launches;        /* Raw launch count */
        double weighted_launches;      /* Weighted count (Enhancement #2) */
        gboolean preloaded;
        pool_type_t pool;              /* Pool classification */
        char *promotion_reason;        /* Why in priority pool (for debugging) */
    } top_apps[STATS_TOP_APPS];
} kp_stats_summary_t;

/**
 * Initialize statistics subsystem
 */
void kp_stats_init(void);

/**
 * Record a preload event
 * @param app_path Path of preloaded application
 */
void kp_stats_record_preload(const char *app_path);

/**
 * Record a hit (app was preloaded when launched)
 * @param app_path Path of launched application
 */
void kp_stats_record_hit(const char *app_path);

/**
 * Record a miss (app was NOT preloaded when launched)
 * @param app_path Path of launched application
 */
void kp_stats_record_miss(const char *app_path);

/**
 * Get current statistics summary
 * @param summary Output structure to fill
 */
void kp_stats_get_summary(kp_stats_summary_t *summary);

/**
 * Record a memory pressure event (Enhancement #5)
 * Called when preloading is skipped due to insufficient memory
 */
void kp_stats_record_memory_pressure(void);

/**
 * Get hit rate for a specific app (Enhancement #5)
 * @param app_path Path of application
 * @return Hit rate (0.0-100.0), or -1.0 if app not tracked
 */
double kp_stats_get_app_hit_rate(const char *app_path);

/**
 * Dump statistics to file
 * @param path File path to write to
 * @return 0 on success, -1 on error
 */
int kp_stats_dump_to_file(const char *path);

/**
 * Free statistics resources
 */
void kp_stats_free(void);

#endif /* STATS_H */
