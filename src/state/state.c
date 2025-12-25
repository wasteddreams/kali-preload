/* state.c - State management implementation for Preheat
 *
 * Based on preload 0.6.4 state.c (VERBATIM helper functions)
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * =============================================================================
 * MODULE OVERVIEW: State Management
 * =============================================================================
 *
 * This is the largest and most central module in Preheat. It manages:
 *
 * 1. DATA STRUCTURES (see state.h for struct definitions)
 *    - kp_map_t: Memory-mapped file regions (shared libraries, binaries)
 *    - kp_exemap_t: Association between executables and their maps
 *    - kp_exe_t: Executable applications being tracked
 *    - kp_markov_t: Correlation chains between pairs of executables
 *    - kp_state_t: Global singleton containing all of the above
 *
 * 2. PERSISTENCE (load/save state to disk)
 *    - Serializes learned patterns to survive daemon restarts
 *    - CRC32 checksums for corruption detection
 *    - Atomic writes via temp file + rename
 *
 * 3. DAEMON LIFECYCLE
 *    - kp_state_run(): Main daemon tick loop scheduling
 *    - kp_state_load(): Initialize from saved state file
 *    - kp_state_save(): Persist current state atomically
 *
 * STATE FILE FORMAT:
 *   Text-based, line-oriented format with tags:
 *   - PRELOAD <version> <time>  - Header with format version
 *   - MAP <seq> <path> <offset> <length> - Memory map region
 *   - BADEXE <time> <size> <path> - Blacklisted small executable
 *   - EXE <seq> <time> <run_time> <path> - Tracked executable
 *   - EXEMAP <exe_seq> <map_seq> <prob> - Exe-to-map association
 *   - MARKOV <exe_a_seq> <exe_b_seq> <time> <prob_matrix> - Correlation
 *   - CRC32 <checksum> - Integrity verification footer
 *
 * MEMORY MANAGEMENT:
 *   - Uses GLib's g_slice allocator for small objects (efficient)
 *   - Reference counting for maps (shared between executables)
 *   - Hash tables for O(1) lookup by path
 *
 * =============================================================================
 */

#include "common.h"
#include "../utils/logging.h"
#include "../utils/crc32.h"
#include "../config/config.h"
#include "../daemon/pause.h"
#include "../daemon/session.h"
#include "state.h"
#include "../monitor/proc.h"
#include "../monitor/spy.h"
#include "../predict/prophet.h"
#include "../utils/seeding.h"

#include <math.h>

/*
 * Global state singleton.
 * Allocated as array of 1 element (same trick as kp_conf) so it can
 * be used like a pointer: kp_state->exes instead of (&kp_state)->exes
 */
kp_state_t kp_state[1];

/* ========================================================================
 * MAP MANAGEMENT FUNCTIONS
 * ========================================================================
 *
 * Memory maps (kp_map_t) represent file regions loaded into a process:
 *   - path: The file path (e.g., /usr/lib/libc.so.6)
 *   - offset: Starting offset within the file
 *   - length: Number of bytes mapped
 *
 * Maps are SHARED between executables via reference counting:
 *   - kp_map_ref() increments refcount and registers in global state
 *   - kp_map_unref() decrements refcount and frees when refcount hits 0
 *
 * The same physical file region can be used by many applications
 * (e.g., libc.so is used by almost everything), so sharing saves memory.
 *
 * ======================================================================== */

/**
 * Create new map object
 *
 * Allocates a new map structure. The map starts with refcount=0
 * and must be registered via kp_map_ref() to be tracked globally.
 *
 * @param path   Absolute path to the mapped file
 * @param offset Byte offset within the file
 * @param length Length of the mapped region in bytes
 * @return       New map object, or NULL on error
 */
kp_map_t *
kp_map_new(const char *path, size_t offset, size_t length)
{
    kp_map_t *map;

    g_return_val_if_fail(path, NULL);

    map = g_slice_new(kp_map_t);
    map->path = g_strdup(path);
    map->offset = offset;
    map->length = length;
    map->refcount = 0;
    map->update_time = kp_state->time;
    map->block = -1;
    return map;
}

/**
 * Free map
 * (VERBATIM from upstream preload_map_free)
 */
void
kp_map_free(kp_map_t *map)
{
    g_return_if_fail(map);
    g_return_if_fail(map->refcount == 0);
    g_return_if_fail(map->path);

    g_free(map->path);
    map->path = NULL;
    g_slice_free(kp_map_t, map);
}

/**
 * Register map in state
 * (VERBATIM from upstream preload_state_register_map)
 */
static void
kp_state_register_map(kp_map_t *map)
{
    g_return_if_fail(!g_hash_table_lookup(kp_state->maps, map));

    map->seq = ++(kp_state->map_seq);
    g_hash_table_insert(kp_state->maps, map, GINT_TO_POINTER(1));
    g_ptr_array_add(kp_state->maps_arr, map);
}

/**
 * Unregister map from state
 * (VERBATIM from upstream preload_state_unregister_map)
 */
static void
kp_state_unregister_map(kp_map_t *map)
{
    g_return_if_fail(g_hash_table_lookup(kp_state->maps, map));

    g_ptr_array_remove(kp_state->maps_arr, map);
    g_hash_table_remove(kp_state->maps, map);
}

/**
 * Reference map (register if needed)
 * (VERBATIM from upstream preload_map_ref)
 */
void
kp_map_ref(kp_map_t *map)
{
    if (!map->refcount)
        kp_state_register_map(map);
    map->refcount++;
}

/**
 * Unreference map (unregister and free if refcount reaches 0)
 * (VERBATIM from upstream preload_map_unref)
 */
void
kp_map_unref(kp_map_t *map)
{
    g_return_if_fail(map);
    g_return_if_fail(map->refcount > 0);

    map->refcount--;
    if (!map->refcount) {
        kp_state_unregister_map(map);
        kp_map_free(map);
    }
}

/**
 * Get map size
 * (VERBATIM from upstream preload_map_get_size)
 */
size_t
kp_map_get_size(kp_map_t *map)
{
    g_return_val_if_fail(map, 0);
    return map->length;
}

/**
 * Hash function for maps
 * (VERBATIM from upstream preload_map_hash)
 */
guint
kp_map_hash(kp_map_t *map)
{
    g_return_val_if_fail(map, 0);
    g_return_val_if_fail(map->path, 0);

    return g_str_hash(map->path)
         + g_direct_hash(GSIZE_TO_POINTER(map->offset))
         + g_direct_hash(GSIZE_TO_POINTER(map->length));
}

/**
 * Equality function for maps
 * (VERBATIM from upstream preload_map_equal)
 */
gboolean
kp_map_equal(kp_map_t *a, kp_map_t *b)
{
    return a->offset == b->offset && a->length == b->length && !strcmp(a->path, b->path);
}

/* ========================================================================
 * EXEMAP MANAGEMENT FUNCTIONS
 * ========================================================================
 *
 * Exemaps (kp_exemap_t) connect executables to the maps they use:
 *
 *   exe ── exemap ──→ map
 *             │
 *             └── prob: probability this map is used when exe runs
 *
 * An executable typically has 10-100 exemaps pointing to:
 *   - The main binary
 *   - Shared libraries (libc, libm, libpthread, etc.)
 *   - Other memory-mapped files
 *
 * The 'prob' field starts at 1.0 and gets updated based on observations.
 * Maps with higher prob are prioritized for preloading.
 *
 * ======================================================================== */

/**
 * Create new exemap linking a map to an executable
 *
 * @param map  The map to reference (will increment map's refcount)
 * @return     New exemap object with prob=1.0
 */
kp_exemap_t *
kp_exemap_new(kp_map_t *map)
{
    kp_exemap_t *exemap;

    g_return_val_if_fail(map, NULL);

    kp_map_ref(map);
    exemap = g_slice_new(kp_exemap_t);
    exemap->map = map;
    exemap->prob = 1.0;
    return exemap;
}

/**
 * Free exemap
 * (VERBATIM from upstream preload_exemap_free)
 */
void
kp_exemap_free(kp_exemap_t *exemap)
{
    g_return_if_fail(exemap);

    if (exemap->map)
        kp_map_unref(exemap->map);
    g_slice_free(kp_exemap_t, exemap);
}

/**
 * Context for exemap iteration
 * (VERBATIM from upstream)
 */
typedef struct _exemap_foreach_context_t
{
    kp_exe_t *exe;
    GHFunc func;
    gpointer data;
} exemap_foreach_context_t;

/**
 * Callback for exemap iteration
 * (VERBATIM from upstream exe_exemap_callback)
 */
static void
exe_exemap_callback(kp_exemap_t *exemap, exemap_foreach_context_t *ctx)
{
    ctx->func(exemap, ctx->exe, ctx->data);
}

/* Wrapper with correct GFunc signature for exe_exemap_callback */
static void
exe_exemap_callback_wrapper(gpointer data, gpointer user_data)
{
    exe_exemap_callback((kp_exemap_t *)data, (exemap_foreach_context_t *)user_data);
}

/**
 * Iterate exemaps for an exe
 * (VERBATIM from upstream exe_exemap_foreach)
 */
static void
exe_exemap_foreach(gpointer G_GNUC_UNUSED key, kp_exe_t *exe, exemap_foreach_context_t *ctx)
{
    ctx->exe = exe;
    g_set_foreach(exe->exemaps, exe_exemap_callback_wrapper, ctx);
}

/* Wrapper with correct GHFunc signature for exe_exemap_foreach */
static void
exe_exemap_foreach_wrapper(gpointer key, gpointer value, gpointer user_data)
{
    exe_exemap_foreach(key, (kp_exe_t *)value, (exemap_foreach_context_t *)user_data);
}

/**
 * Iterate all exemaps
 * (VERBATIM from upstream preload_exemap_foreach)
 */
void
kp_exemap_foreach(GHFunc func, gpointer user_data)
{
    exemap_foreach_context_t ctx;
    ctx.func = func;
    ctx.data = user_data;
    g_hash_table_foreach(kp_state->exes, exe_exemap_foreach_wrapper, &ctx);
}

/* ========================================================================
 * MARKOV CHAIN MANAGEMENT FUNCTIONS
 * ========================================================================
 *
 * Markov chains (kp_markov_t) model correlations between application pairs:
 *
 *   exe_a ←───── markov ─────→ exe_b
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
 * ======================================================================== */

/**
 * Create new Markov chain between two executables
 *
 * @param a          First executable
 * @param b          Second executable (must differ from a)
 * @param initialize If TRUE, initialize state based on current running status
 * @return           New markov object, added to both exe's markov sets
 */
kp_markov_t *
kp_markov_new(kp_exe_t *a, kp_exe_t *b, gboolean initialize)
{
    kp_markov_t *markov;

    g_return_val_if_fail(a, NULL);
    g_return_val_if_fail(b, NULL);
    g_return_val_if_fail(a != b, NULL);

    markov = g_slice_new(kp_markov_t);
    markov->a = a;
    markov->b = b;

    if (initialize) {
        markov->state = markov_state(markov);

        markov->change_timestamp = kp_state->time;
        if (a->change_timestamp > 0 && b->change_timestamp > 0) {
            if (a->change_timestamp < kp_state->time)
                markov->change_timestamp = a->change_timestamp;
            if (b->change_timestamp < kp_state->time && b->change_timestamp > markov->change_timestamp)
                markov->change_timestamp = a->change_timestamp;
            if (a->change_timestamp > markov->change_timestamp)
                markov->state ^= 1;
            if (b->change_timestamp > markov->change_timestamp)
                markov->state ^= 2;
        }

        markov->time = 0;
        memset(markov->time_to_leave, 0, sizeof(markov->time_to_leave));
        memset(markov->weight, 0, sizeof(markov->weight));
        kp_markov_state_changed(markov);
    }

    g_set_add(a->markovs, markov);
    g_set_add(b->markovs, markov);
    return markov;
}

/**
 * Handle Markov state change
 * (VERBATIM from upstream preload_markov_state_changed)
 */
void
kp_markov_state_changed(kp_markov_t *markov)
{
    int old_state, new_state;

    if (markov->change_timestamp == kp_state->time)
        return; /* already taken care of */

    old_state = markov->state;
    new_state = markov_state(markov);

    g_return_if_fail(old_state != new_state);

    markov->weight[old_state][old_state]++;
    markov->time_to_leave[old_state] += ((kp_state->time - markov->change_timestamp)
                                         - markov->time_to_leave[old_state])
                                        / markov->weight[old_state][old_state];

    markov->weight[old_state][new_state]++;
    markov->state = new_state;
    markov->change_timestamp = kp_state->time;
}

/**
 * Free Markov chain
 * (VERBATIM from upstream preload_markov_free)
 */
void
kp_markov_free(kp_markov_t *markov, kp_exe_t *from)
{
    g_return_if_fail(markov);

    if (from) {
        kp_exe_t *other;
        g_assert(markov->a == from || markov->b == from);
        other = markov_other_exe(markov, from);
        g_set_remove(other->markovs, markov);
    } else {
        g_set_remove(markov->a->markovs, markov);
        g_set_remove(markov->b->markovs, markov);
    }
    g_slice_free(kp_markov_t, markov);
}

/**
 * Context for markov iteration
 * (VERBATIM from upstream)
 */
typedef struct _markov_foreach_context_t
{
    kp_exe_t *exe;
    GFunc func;
    gpointer data;
} markov_foreach_context_t;

/**
 * Callback for markov iteration
 * (VERBATIM from upstream exe_markov_callback)
 */
static void
exe_markov_callback(kp_markov_t *markov, markov_foreach_context_t *ctx)
{
    /* Each markov should be processed only once, not twice */
    if (ctx->exe == markov->a)
        ctx->func(markov, ctx->data);
}

/* Wrapper with correct GFunc signature for exe_markov_callback */
static void
exe_markov_callback_wrapper(gpointer data, gpointer user_data)
{
    exe_markov_callback((kp_markov_t *)data, (markov_foreach_context_t *)user_data);
}

/**
 * Iterate markovs for an exe
 * (VERBATIM from upstream exe_markov_foreach)
 */
static void
exe_markov_foreach(gpointer G_GNUC_UNUSED key, kp_exe_t *exe, markov_foreach_context_t *ctx)
{
    ctx->exe = exe;
    g_set_foreach(exe->markovs, exe_markov_callback_wrapper, ctx);
}

/* Wrapper with correct GHFunc signature for exe_markov_foreach */
static void
exe_markov_foreach_wrapper(gpointer key, gpointer value, gpointer user_data)
{
    exe_markov_foreach(key, (kp_exe_t *)value, (markov_foreach_context_t *)user_data);
}

/**
 * Iterate all markovs
 * (VERBATIM from upstream preload_markov_foreach)
 */
void
kp_markov_foreach(GFunc func, gpointer user_data)
{
    markov_foreach_context_t ctx;
    ctx.func = func;
    ctx.data = user_data;
    g_hash_table_foreach(kp_state->exes, exe_markov_foreach_wrapper, &ctx);
}

/**
 * Calculate correlation coefficient
 * (VERBATIM from upstream preload_markov_correlation)
 *
 * Calculates Pearson product-moment correlation coefficient between
 * two exes being run. Returns value in range -1 to 1.
 */
double
kp_markov_correlation(kp_markov_t *markov)
{
    double correlation, numerator, denominator2;
    int t, a, b, ab;

    t = kp_state->time;
    a = markov->a->time;
    b = markov->b->time;
    ab = markov->time;

    if (a == 0 || a == t || b == 0 || b == t)
        correlation = 0;
    else {
        numerator = ((double)t*ab) - ((double)a * b);
        denominator2 = ((double)a * b) * ((double)(t - a) * (t - b));
        correlation = numerator / sqrt(denominator2);
    }

    g_assert(fabs(correlation) <= 1.00001);
    return correlation;
}

/* ========================================================================
 * EXE MANAGEMENT FUNCTIONS
 * ========================================================================
 *
 * Executables (kp_exe_t) represent tracked applications:
 *
 *   exe.path     = "/usr/bin/firefox"
 *   exe.time     = total seconds ever running (for frequency weighting)
 *   exe.exemaps  = set of memory maps this exe uses
 *   exe.markovs  = set of correlations with other exes
 *
 * EXE LIFECYCLE:
 *   1. Discovered via /proc scan → kp_exe_new()
 *   2. Registered in global state → kp_state_register_exe()
 *   3. Markov chains created to all existing exes
 *   4. Time/prob updated each scan cycle
 *   5. Serialized to state file on save
 *
 * The 'seq' field provides stable ordering across save/load cycles
 * since hash table iteration order is not deterministic.
 *
 * ======================================================================== */

/**
 * Add map size to exe's total size
 * Helper callback for calculating total memory footprint of an executable.
 */
static void
exe_add_map_size(kp_exemap_t *exemap, kp_exe_t *exe)
{
    exe->size += kp_map_get_size(exemap->map);
}

/* Wrapper with correct GFunc signature for exe_add_map_size */
static void
exe_add_map_size_wrapper(gpointer data, gpointer user_data)
{
    exe_add_map_size((kp_exemap_t *)data, (kp_exe_t *)user_data);
}

/**
 * Create new executable object
 *
 * @param path     Absolute path to the executable
 * @param running  TRUE if executable is currently running
 * @param exemaps  Pre-populated set of exemaps, or NULL to create empty
 * @return         New exe object (not yet registered in global state)
 */
kp_exe_t *
kp_exe_new(const char *path, gboolean running, GSet *exemaps)
{
    kp_exe_t *exe;

    g_return_val_if_fail(path, NULL);

    exe = g_slice_new(kp_exe_t);
    exe->path = g_strdup(path);
    exe->size = 0;
    exe->time = 0;
    exe->change_timestamp = kp_state->time;

    /* Initialize weighted launch counting fields (Enhancement #2) */
    exe->weighted_launches = 0.0;
    exe->raw_launches = 0;
    exe->total_duration_sec = 0;
    exe->running_pids = g_hash_table_new_full(
        g_direct_hash, g_direct_equal,
        NULL,                /* pid is stored as GINT_TO_POINTER, no need to free */
        g_free               /* process_info_t* allocated with g_new, free with g_free */
    );

    if (running) {
        exe->update_time = exe->running_timestamp = kp_state->last_running_timestamp;
    } else {
        exe->update_time = exe->running_timestamp = -1;
    }

    if (!exemaps)
        exe->exemaps = g_set_new();
    else
        exe->exemaps = exemaps;

    g_set_foreach(exe->exemaps, exe_add_map_size_wrapper, exe);
    exe->markovs = g_set_new();
    return exe;
}

/* Wrapper with correct GFunc signature for kp_exemap_free */
static void
kp_exemap_free_wrapper(gpointer data, gpointer user_data)
{
    (void)user_data;
    kp_exemap_free((kp_exemap_t *)data);
}

/* Wrapper with correct GFunc signature for kp_markov_free */
static void
kp_markov_free_from_exe_wrapper(gpointer data, gpointer user_data)
{
    kp_markov_free((kp_markov_t *)data, (kp_exe_t *)user_data);
}

/**
 * Free exe
 * (VERBATIM from upstream preload_exe_free, with running_pids cleanup)
 */
void
kp_exe_free(kp_exe_t *exe)
{
    g_return_if_fail(exe);
    g_return_if_fail(exe->path);

    g_set_foreach(exe->exemaps, kp_exemap_free_wrapper, NULL);
    g_set_free(exe->exemaps);
    exe->exemaps = NULL;

    g_set_foreach(exe->markovs, kp_markov_free_from_exe_wrapper, exe);
    g_set_free(exe->markovs);
    exe->markovs = NULL;

    /* Free running PIDs hash table (Enhancement #2) */
    if (exe->running_pids) {
        g_hash_table_destroy(exe->running_pids);
        exe->running_pids = NULL;
    }

    g_free(exe->path);
    exe->path = NULL;
    g_slice_free(kp_exe_t, exe);
}

/**
 * Create exemap and add to exe
 * (VERBATIM from upstream preload_exe_map_new)
 */
kp_exemap_t *
kp_exe_map_new(kp_exe_t *exe, kp_map_t *map)
{
    kp_exemap_t *exemap;

    g_return_val_if_fail(exe, NULL);
    g_return_val_if_fail(map, NULL);

    exemap = kp_exemap_new(map);
    g_set_add(exe->exemaps, exemap);
    exe_add_map_size(exemap, exe);
    return exemap;
}

/**
 * Helper for creating markov with existing exe
 * (VERBATIM from upstream shift_preload_markov_new)
 */
static void
shift_kp_markov_new(gpointer G_GNUC_UNUSED key, kp_exe_t *a, kp_exe_t *b)
{
    if (a != b)
        kp_markov_new(a, b, TRUE);
}

/* Wrapper with correct GHFunc signature for shift_kp_markov_new */
static void
shift_kp_markov_new_wrapper(gpointer key, gpointer value, gpointer user_data)
{
    shift_kp_markov_new(key, (kp_exe_t *)value, (kp_exe_t *)user_data);
}

/**
 * Register exe in state
 * (VERBATIM from upstream preload_state_register_exe)
 */
void
kp_state_register_exe(kp_exe_t *exe, gboolean create_markovs)
{
    g_return_if_fail(!g_hash_table_lookup(kp_state->exes, exe));

    exe->seq = ++(kp_state->exe_seq);
    if (create_markovs) {
        g_hash_table_foreach(kp_state->exes, shift_kp_markov_new_wrapper, exe);
    }
    g_hash_table_insert(kp_state->exes, exe->path, exe);
}

/**
 * Unregister exe from state
 * (VERBATIM from upstream preload_state_unregister_exe)
 */
void
kp_state_unregister_exe(kp_exe_t *exe)
{
    g_return_if_fail(g_hash_table_lookup(kp_state->exes, exe));

    g_set_foreach(exe->markovs, kp_markov_free_from_exe_wrapper, exe);
    g_set_free(exe->markovs);
    exe->markovs = NULL;
    g_hash_table_remove(kp_state->exes, exe);
}

/* ========================================================================
 * FAMILY MANAGEMENT FUNCTIONS (Enhancement #3)
 * ========================================================================
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
 * ======================================================================== */

/**
 * Create new application family
 *
 * @param family_id  Unique identifier (e.g., "firefox")
 * @param method     How this family was discovered
 * @return           New family object (not yet registered)
 */
kp_app_family_t *
kp_family_new(const char *family_id, discovery_method_t method)
{
    kp_app_family_t *family;

    g_return_val_if_fail(family_id, NULL);

    family = g_new0(kp_app_family_t, 1);
    family->family_id = g_strdup(family_id);
    family->member_paths = g_ptr_array_new_with_free_func(g_free);
    family->method = method;
    
    /* Stats will be computed on demand */
    family->total_weighted_launches = 0.0;
    family->total_raw_launches = 0;
    family->last_used = 0;

    return family;
}

/**
 * Free application family
 */
void
kp_family_free(kp_app_family_t *family)
{
    g_return_if_fail(family);

    g_free(family->family_id);
    g_ptr_array_free(family->member_paths, TRUE);
    g_free(family);
}

/**
 * Add member to family
 *
 * @param family   Family to add to
 * @param exe_path Executable path to add as member
 */
void
kp_family_add_member(kp_app_family_t *family, const char *exe_path)
{
    g_return_if_fail(family);
    g_return_if_fail(exe_path);

    /* Check for duplicates */
    for (guint i = 0; i < family->member_paths->len; i++) {
        if (strcmp(g_ptr_array_index(family->member_paths, i), exe_path) == 0) {
            return;  /* Already a member */
        }
    }

    g_ptr_array_add(family->member_paths, g_strdup(exe_path));
    
    /* Register reverse mapping */
    if (kp_state->exe_to_family) {
        g_hash_table_insert(kp_state->exe_to_family, 
                            g_strdup(exe_path), 
                            g_strdup(family->family_id));
    }
}

/**
 * Update family statistics by aggregating from all members
 */
void
kp_family_update_stats(kp_app_family_t *family)
{
    g_return_if_fail(family);

    family->total_weighted_launches = 0.0;
    family->total_raw_launches = 0;
    family->last_used = 0;

    for (guint i = 0; i < family->member_paths->len; i++) {
        const char *exe_path = g_ptr_array_index(family->member_paths, i);
        kp_exe_t *exe = g_hash_table_lookup(kp_state->exes, exe_path);
        
        if (exe) {
            family->total_weighted_launches += exe->weighted_launches;
            family->total_raw_launches += exe->raw_launches;
            
            /* Track most recent usage */
            if (exe->running_timestamp > (int)family->last_used) {
                family->last_used = exe->running_timestamp;
            }
        }
    }
}

/**
 * Lookup family by ID
 *
 * @param family_id  Family identifier
 * @return           Family object or NULL if not found
 */
kp_app_family_t *
kp_family_lookup(const char *family_id)
{
    g_return_val_if_fail(family_id, NULL);
    g_return_val_if_fail(kp_state->app_families, NULL);

    return g_hash_table_lookup(kp_state->app_families, family_id);
}

/**
 * Lookup family ID by executable path
 *
 * @param exe_path  Executable path
 * @return          Family ID or NULL if exe not in any family
 */
const char *
kp_family_lookup_by_exe(const char *exe_path)
{
    g_return_val_if_fail(exe_path, NULL);
    /* Note: kp_state is a static array, no NULL check needed */
    g_return_val_if_fail(kp_state->exe_to_family, NULL);

    return g_hash_table_lookup(kp_state->exe_to_family, exe_path);
}

/* ========================================================================
 * STATE FILE FORMAT (VERBATIM from upstream)
 * ======================================================================== */

#define TAG_PRELOAD     "PRELOAD"
#define TAG_MAP         "MAP"
#define TAG_BADEXE      "BADEXE"
#define TAG_EXE         "EXE"
#define TAG_EXEMAP      "EXEMAP"
#define TAG_MARKOV      "MARKOV"
#define TAG_FAMILY      "FAMILY"
#define TAG_CRC32       "CRC32"

#define READ_TAG_ERROR              "invalid tag"
#define READ_SYNTAX_ERROR           "invalid syntax"
#define READ_INDEX_ERROR            "invalid index"
#define READ_DUPLICATE_INDEX_ERROR  "duplicate index"
#define READ_DUPLICATE_OBJECT_ERROR "duplicate object"
#define READ_CRC_ERROR              "CRC32 checksum mismatch"

typedef struct _read_context_t
{
    char *line;
    const char *errmsg;
    char *path;
    GHashTable *maps;
    GHashTable *exes;
    gpointer data;
    GError *err;
    char filebuf[FILELEN];
} read_context_t;

/* Read map from state file (VERBATIM from upstream) */
static void
read_map(read_context_t *rc)
{
    kp_map_t *map;
    int update_time;
    int i, expansion;
    unsigned long offset, length;
    char *path;

    if (6 > sscanf(rc->line,
                   "%d %d %lu %lu %d %"FILELENSTR"s",
                   &i, &update_time, &offset, &length, &expansion, rc->filebuf)) {
        rc->errmsg = READ_SYNTAX_ERROR;
        return;
    }

    path = g_filename_from_uri(rc->filebuf, NULL, &(rc->err));
    if (!path)
        return;

    map = kp_map_new(path, offset, length);
    g_free(path);
    if (g_hash_table_lookup(rc->maps, GINT_TO_POINTER(i))) {
        rc->errmsg = READ_DUPLICATE_INDEX_ERROR;
        goto err;
    }
    if (g_hash_table_lookup(kp_state->maps, map)) {
        rc->errmsg = READ_DUPLICATE_OBJECT_ERROR;
        goto err;
    }

    map->update_time = update_time;
    kp_map_ref(map);
    g_hash_table_insert(rc->maps, GINT_TO_POINTER(i), map);
    return;

err:
    kp_map_free(map);
}

/* Read bad exe from state file (VERBATIM from upstream) */
static void
read_badexe(read_context_t *rc G_GNUC_UNUSED)
{
    /* We do not read-in badexes. Let's clean them up on every start, give them
     * another chance! */
    return;
}

/* Read exe from state file (Enhanced with weighted launch counting) */
static void
read_exe(read_context_t *rc)
{
    kp_exe_t *exe;
    int update_time, time;
    int i, expansion, pool = POOL_OBSERVATION;
    double weighted_launches = 0.0;
    unsigned long raw_launches = 0, total_duration = 0;
    char *path;
    int fields_read;

    /* Try new 9-field format first (with weighted counting) */
    fields_read = sscanf(rc->line,
                         "%d %d %d %d %d %lf %lu %lu %"FILELENSTR"s",
                         &i, &update_time, &time, &expansion, &pool, 
                         &weighted_launches, &raw_launches, &total_duration, rc->filebuf);
    
    if (fields_read >= 9) {
        /* Success - new format */
        g_debug("Read exe in new 9-field format (weighted counting)");
    } else {
        /* Try 6-field format (pool but no weighted counting) */
        fields_read = sscanf(rc->line,
                             "%d %d %d %d %d %"FILELENSTR"s",
                             &i, &update_time, &time, &expansion, &pool, rc->filebuf);
        
        if (fields_read >= 6) {
            /* Migrating from pool-only format */
            g_debug("Migrated 6-field exe entry (pool only): %s", rc->filebuf);
        } else {
            /* Fall back to old 5-field format (original format) */
            if (5 > sscanf(rc->line,
                           "%d %d %d %d %"FILELENSTR"s",
                           &i, &update_time, &time, &expansion, rc->filebuf)) {
                rc->errmsg = READ_SYNTAX_ERROR;
                return;
            }
            pool = POOL_OBSERVATION;  /* Default for migrated apps */
            g_debug("Migrated old 5-field exe entry to observation pool: %s", rc->filebuf);
        }
    }

    path = g_filename_from_uri(rc->filebuf, NULL, &(rc->err));
    if (!path)
        return;

    exe = kp_exe_new(path, FALSE, NULL);
    exe->pool = pool;  /* Set pool classification */
    exe->weighted_launches = weighted_launches;
    exe->raw_launches = raw_launches;
    exe->total_duration_sec = total_duration;
    exe->change_timestamp = -1;
    g_free(path);
    if (g_hash_table_lookup(rc->exes, GINT_TO_POINTER(i))) {
        rc->errmsg = READ_DUPLICATE_INDEX_ERROR;
        goto err;
    }
    if (g_hash_table_lookup(kp_state->exes, exe->path)) {
        rc->errmsg = READ_DUPLICATE_OBJECT_ERROR;
        goto err;
    }

    exe->update_time = update_time;
    exe->time = time;
    g_hash_table_insert(rc->exes, GINT_TO_POINTER(i), exe);
    kp_state_register_exe(exe, FALSE);
    return;

err:
    kp_exe_free(exe);
}

/* Read exemap from state file (VERBATIM from upstream) */
static void
read_exemap(read_context_t *rc)
{
    int iexe, imap;
    kp_exe_t *exe;
    kp_map_t *map;
    kp_exemap_t *exemap;
    double prob;

    if (3 > sscanf(rc->line,
                   "%d %d %lg",
                   &iexe, &imap, &prob)) {
        rc->errmsg = READ_SYNTAX_ERROR;
        return;
    }

    exe = g_hash_table_lookup(rc->exes, GINT_TO_POINTER(iexe));
    map = g_hash_table_lookup(rc->maps, GINT_TO_POINTER(imap));
    if (!exe || !map) {
        rc->errmsg = READ_INDEX_ERROR;
        return;
    }

    exemap = kp_exe_map_new(exe, map);
    exemap->prob = prob;
}

/* Read markov from state file (VERBATIM from upstream) */
static void
read_markov(read_context_t *rc)
{
    int time, state, state_new;
    int ia, ib;
    kp_exe_t *a, *b;
    kp_markov_t *markov;
    int n;

    n = 0;
    if (3 > sscanf(rc->line,
                   "%d %d %d%n",
                   &ia, &ib, &time, &n)) {
        rc->errmsg = READ_SYNTAX_ERROR;
        return;
    }
    rc->line += n;

    a = g_hash_table_lookup(rc->exes, GINT_TO_POINTER(ia));
    b = g_hash_table_lookup(rc->exes, GINT_TO_POINTER(ib));
    if (!a || !b) {
        rc->errmsg = READ_INDEX_ERROR;
        return;
    }

    markov = kp_markov_new(a, b, FALSE);
    markov->time = time;

    for (state = 0; state < 4; state++) {
        double x;
        if (1 > sscanf(rc->line,
                       "%lg%n",
                       &x, &n)) {
            rc->errmsg = READ_SYNTAX_ERROR;
            goto err;  /* AUDIT FIX M-3: cleanup allocated markov */
        }

        rc->line += n;
        markov->time_to_leave[state] = x;
    }
    for (state = 0; state < 4; state++) {
        for (state_new = 0; state_new < 4; state_new++) {
            int x;
            if (1 > sscanf(rc->line,
                           "%d%n",
                           &x, &n)) {
                rc->errmsg = READ_SYNTAX_ERROR;
                goto err;  /* AUDIT FIX M-3: cleanup allocated markov */
            }

            rc->line += n;
            markov->weight[state][state_new] = x;
        }
    }
    return;  /* Success path */

err:
    /* Free markov struct to prevent memory leak on parse error */
    kp_markov_free(markov, NULL);
}

/* Read CRC32 footer from state file
 * Just accepts the tag to avoid "invalid tag" error.
 * Actual CRC validation happens separately if needed.
 */
static void
read_crc32(read_context_t *rc)
{
    unsigned int stored_crc;
    
    /* Parse the CRC value but don't validate - just accept the tag */
    if (1 > sscanf(rc->line, "%x", &stored_crc)) {
        /* CRC line malformed, but not fatal - just log and continue */
        g_debug("CRC32 line malformed, ignoring");
    }
    /* CRC32 tag successfully parsed - no error */
}

/* Read family from state file (Enhancement #3) */
static void
read_family(read_context_t *rc)
{
    char family_id[256];
    int method_int;
    char members_str[4096];
    kp_app_family_t *family;
    char *member_token;
    
    if (3 > sscanf(rc->line, "%255s %d %4095[^\n]", family_id, &method_int, members_str)) {
        rc->errmsg = READ_SYNTAX_ERROR;
        return;
    }
    
    family = kp_family_new(family_id, (discovery_method_t)method_int);
    
    /* Parse semicolon-separated member paths */
    member_token = strtok(members_str, ";");
    while (member_token) {
        /* Skip whitespace */
        while (*member_token == ' ') member_token++;
        if (*member_token) {
            kp_family_add_member(family, member_token);
        }
        member_token = strtok(NULL, ";");
    }
    
    /* Register family in state */
    g_hash_table_insert(kp_state->app_families, g_strdup(family_id), family);
}

/* Helper callbacks for state initialization (VERBATIM from upstream) */
static void
set_running_process_callback(pid_t G_GNUC_UNUSED pid, const char *path, int time)
{
    kp_exe_t *exe;

    exe = g_hash_table_lookup(kp_state->exes, path);
    if (exe) {
        exe->running_timestamp = time;
        kp_state->running_exes = g_slist_prepend(kp_state->running_exes, exe);
    }
}

static void
set_markov_state_callback(kp_markov_t *markov)
{
    markov->state = markov_state(markov);
}

/* Wrapper with correct GHFunc signature for set_running_process_callback */
static void
set_running_process_callback_wrapper(gpointer key, gpointer value, gpointer user_data)
{
    (void)key;  /* pid - unused by set_running_process_callback */
    /* kp_proc_foreach: key = pid (GUINT_TO_POINTER), value = exe path */
    set_running_process_callback(0, (const char *)value, GPOINTER_TO_INT(user_data));
}

/* Wrapper with correct GFunc signature for set_markov_state_callback */
static void
set_markov_state_callback_wrapper(gpointer data, gpointer user_data)
{
    (void)user_data;
    set_markov_state_callback((kp_markov_t *)data);
}

/**
 * Handle corrupt state file by renaming it and logging
 * Returns TRUE if state file can be recovered (fresh start)
 */
static gboolean
handle_corrupt_statefile(const char *statefile, const char *reason)
{
    char *broken_path;
    time_t now;
    struct tm *tm_info;
    char timestamp[32];

    now = time(NULL);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);

    broken_path = g_strdup_printf("%s.broken.%s", statefile, timestamp);

    if (rename(statefile, broken_path) == 0) {
        g_warning("State file corrupt (%s), renamed to %s - starting fresh",
                  reason, broken_path);
    } else {
        g_warning("State file corrupt (%s), could not rename: %s - starting fresh",
                  reason, strerror(errno));
    }

    g_free(broken_path);
    return TRUE;  /* OK to continue with fresh state */
}

/* Read state from GIOChannel (VERBATIM from upstream) */
static char *
read_state(GIOChannel *f)
{
    int lineno;
    GString *linebuf;
    GIOStatus s;
    char tag[32] = "";
    char *errmsg;

    read_context_t rc;

    rc.errmsg = NULL;
    rc.err = NULL;
    rc.maps = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)kp_map_unref);
    rc.exes = g_hash_table_new(g_direct_hash, g_direct_equal);

    linebuf = g_string_sized_new(100);
    lineno = 0;

    while (!rc.err && !rc.errmsg) {
        s = g_io_channel_read_line_string(f, linebuf, NULL, &rc.err);
        if (s == G_IO_STATUS_AGAIN)
            continue;
        if (s == G_IO_STATUS_EOF || s == G_IO_STATUS_ERROR)
            break;

        lineno++;
        rc.line = linebuf->str;

        if (1 > sscanf(rc.line,
                       "%31s",
                       tag)) {
            rc.errmsg = READ_TAG_ERROR;
            break;
        }
        rc.line += strlen(tag);

        if (lineno == 1 && strcmp(tag, TAG_PRELOAD)) {
            g_warning("State file has invalid header, ignoring it");
            break;
        }

        if (!strcmp(tag, TAG_PRELOAD)) {
            int major_ver_read, major_ver_run;
            const char *version;
            int time;

            if (lineno != 1 || 2 > sscanf(rc.line,
                                          "%d.%*[^\t]\t%d",
                                          &major_ver_read, &time)) {
                rc.errmsg = READ_SYNTAX_ERROR;
                break;
            }

            version = VERSION;
            major_ver_run = strtod(version, NULL);

            if (major_ver_run < major_ver_read) {
                g_warning("State file is of a newer version, ignoring it");
                break;
            } else if (major_ver_run > major_ver_read) {
                g_warning("State file is of an old version that I cannot understand anymore, ignoring it");
                break;
            }

            kp_state->last_accounting_timestamp = kp_state->time = time;
        }
        else if (!strcmp(tag, TAG_MAP))    read_map(&rc);
        else if (!strcmp(tag, TAG_BADEXE)) read_badexe(&rc);
        else if (!strcmp(tag, TAG_EXE))    read_exe(&rc);
        else if (!strcmp(tag, TAG_EXEMAP)) read_exemap(&rc);
        else if (!strcmp(tag, TAG_MARKOV)) read_markov(&rc);
        else if (!strcmp(tag, TAG_FAMILY)) read_family(&rc);  /* Enhancement #3 */
        else if (!strcmp(tag, TAG_CRC32))  read_crc32(&rc);  /* Handle CRC32 footer */
        else if (linebuf->str[0] && linebuf->str[0] != '#') {
            rc.errmsg = READ_TAG_ERROR;
            break;
        }
    }

    g_string_free(linebuf, TRUE);
    g_hash_table_destroy(rc.exes);
    g_hash_table_destroy(rc.maps);

    if (rc.err)
        rc.errmsg = rc.err->message;
    if (rc.errmsg)
        errmsg = g_strdup_printf("line %d: %s", lineno, rc.errmsg);
    else
        errmsg = NULL;
    if (rc.err)
        g_error_free(rc.err);

    if (!errmsg) {
        kp_proc_foreach(set_running_process_callback_wrapper, GINT_TO_POINTER(kp_state->time));
        kp_state->last_running_timestamp = kp_state->time;
        kp_markov_foreach(set_markov_state_callback_wrapper, NULL);
    }

    return errmsg;
}

/**
 * Load state from file
 * Modified from upstream to handle corruption gracefully and seed on first run
 */
void kp_state_load(const char *statefile)
{
    gboolean state_was_empty = FALSE;
    
    memset(kp_state, 0, sizeof(*kp_state));
    kp_state->exes = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)kp_exe_free);
    kp_state->bad_exes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    kp_state->maps = g_hash_table_new((GHashFunc)kp_map_hash, (GEqualFunc)kp_map_equal);
    kp_state->maps_arr = g_ptr_array_new();

    /* Initialize family hash tables (Enhancement #3) */
    kp_state->app_families = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                     g_free, (GDestroyNotify)kp_family_free);
    kp_state->exe_to_family = g_hash_table_new_full(g_str_hash, g_str_equal, 
                                                      g_free, g_free);

    if (statefile && *statefile) {
        GIOChannel *f;
        GError *err = NULL;

        g_message("loading state from %s", statefile);

        f = g_io_channel_new_file(statefile, "r", &err);
        if (!f) {
            if (err->code == G_FILE_ERROR_ACCES) {
                /* Permission denied - this is a configuration problem, warn but continue */
                g_critical("cannot open %s for reading: %s - continuing without saved state",
                           statefile, err->message);
            } else if (err->code == G_FILE_ERROR_NOENT) {
                /* File doesn't exist - first run */
                g_message("State file not found - first run detected");
                state_was_empty = TRUE;
            } else {
                /* Other non-fatal error */
                g_warning("cannot open %s for reading, ignoring: %s", statefile, err->message);
            }
            g_error_free(err);
        } else {
            char *errmsg;

            errmsg = read_state(f);
            g_io_channel_unref(f);
            if (errmsg) {
                /* Corruption detected - rename broken file and start fresh */
                handle_corrupt_statefile(statefile, errmsg);
                g_free(errmsg);
                state_was_empty = TRUE;  /* Treat corruption as empty state */
                /* Already warned in handle_corrupt_statefile, state remains fresh */
            }
        }

        g_debug("loading state done");
    }

    /* Enhancement #4: Smart first-run seeding */
    if (state_was_empty || (kp_state->exes && g_hash_table_size(kp_state->exes) == 0)) {
        kp_seed_from_sources();
    }

    kp_proc_get_memstat(&(kp_state->memstat));
    kp_state->memstat_timestamp = kp_state->time;
}

/**
 * Register manual apps that aren't already tracked
 * 
 * This allows manual apps to be preloaded even if never run before.
 * Called after kp_state_load() and on SIGHUP (config reload).
 *
 * Note: Apps registered this way won't have memory maps until first run,
 * but their existence in kp_state->exes allows boost_manual_apps() to work.
 */
void
kp_state_register_manual_apps(void)
{
    char **app_path;
    int registered = 0;
    int already_tracked = 0;
    int total = 0;
    
    if (!kp_conf->system.manual_apps_loaded ||
        kp_conf->system.manual_apps_count == 0) {
        g_debug("No manual apps configured");
        return;
    }
    
    g_message("=== Registering manual apps ===");
    
    for (app_path = kp_conf->system.manual_apps_loaded; *app_path; app_path++) {
        kp_exe_t *exe;
        total++;
        
        /* Check if already tracked */
        exe = g_hash_table_lookup(kp_state->exes, *app_path);
        if (exe) {
            g_debug("Manual app already tracked: %s", *app_path);
            already_tracked++;
            continue;
        }
        
        /* Create new exe entry (not running, empty exemaps) */
        exe = kp_exe_new(*app_path, FALSE, NULL);
        if (!exe) {
            g_warning("Failed to create exe for manual app: %s", *app_path);
            continue;
        }
        
        /* Register without creating markov chains initially */
        kp_state_register_exe(exe, FALSE);
        registered++;
        
        g_message("Registered manual app: %s", *app_path);
    }
    
    if (registered > 0 || already_tracked > 0) {
        g_message("Manual apps: %d registered, %d already tracked (of %d total)",
                  registered, already_tracked, total);
    }
    
    if (registered > 0) {
        kp_state->dirty = TRUE;  /* Save state with new apps */
    }
}

/* ========================================================================
 * STATE WRITE FUNCTIONS - Serializing State to Disk
 * ========================================================================
 *
 * These functions serialize the in-memory state to a text file.
 * 
 * WRITE SEQUENCE:
 *   1. write_header()  - Version info and total running time
 *   2. write_map()     - All memory map regions (shared by exes)
 *   3. write_badexe()  - Blacklisted small executables
 *   4. write_exe()     - All tracked executables
 *   5. write_exemap()  - Exe-to-map associations with probabilities
 *   6. write_markov()  - Correlation chains between exe pairs
 *   7. write_crc32()   - Integrity checksum footer
 *
 * ATOMIC SAVE:
 *   State is written to statefile.tmp, then renamed to statefile.
 *   This prevents corruption from interrupted writes or power loss.
 *
 * DATA SAFETY:
 *   - fsync() called before rename to ensure data hits disk
 *   - CRC32 computed over entire file for corruption detection
 *
 * ======================================================================== */

#define write_it(s) \
    if (wc->err || G_IO_STATUS_NORMAL != g_io_channel_write_chars(wc->f, s, -1, NULL, &(wc->err))) \
        return;
#define write_tag(tag) write_it(tag "\t")
#define write_string(string) write_it((string)->str)
#define write_ln() write_it("\n")

typedef struct _write_context_t
{
    GIOChannel *f;
    GString *line;
    GError *err;
} write_context_t;

/* Write header (VERBATIM from upstream write_header) */
/* Write header (VERBATIM from upstream write_header) */
static void
write_header(write_context_t *wc)
{
    write_tag(TAG_PRELOAD);
    g_string_printf(wc->line,
                    "%s\t%d",
                    VERSION, kp_state->time);
    write_string(wc->line);
    write_ln();
}

/* Write map (VERBATIM from upstream write_map) */
static void
write_map(kp_map_t *map, gpointer G_GNUC_UNUSED data, write_context_t *wc)
{
    char *uri;

    uri = g_filename_to_uri(map->path, NULL, &(wc->err));
    if (!uri)
        return;

    write_tag(TAG_MAP);
    g_string_printf(wc->line,
                    "%d\t%d\t%lu\t%lu\t%d\t%s",
                    map->seq, map->update_time, (long)map->offset, (long)map->length, -1/*expansion*/, uri);
    write_string(wc->line);
    write_ln();

    g_free(uri);
}

static void
write_badexe(char *path, int update_time, write_context_t *wc)
{
    char *uri;

    uri = g_filename_to_uri(path, NULL, &(wc->err));
    if (!uri)
        return;

    write_tag(TAG_BADEXE);
    g_string_printf(wc->line,
                    "%d\t%d\t%s",
                    update_time, -1/*expansion*/, uri);
    write_string(wc->line);
    write_ln();

    g_free(uri);
}

/* Wrapper with correct GHFunc signature for write_badexe */
static void
write_badexe_wrapper(gpointer key, gpointer value, gpointer user_data)
{
    write_badexe((char *)key, GPOINTER_TO_INT(value), (write_context_t *)user_data);
}

/* Write exe (Enhanced with weighted launch counting) */
static void
write_exe(gpointer G_GNUC_UNUSED key, kp_exe_t *exe, write_context_t *wc)
{
    char *uri;

    uri = g_filename_to_uri(exe->path, NULL, &(wc->err));
    if (!uri)
        return;

    write_tag(TAG_EXE);
    /* Format: seq update_time time expansion pool weighted_launches raw_launches duration path */
    g_string_printf(wc->line,
                    "%d\t%d\t%d\t%d\t%d\t%.6f\t%lu\t%lu\t%s",
                    exe->seq, exe->update_time, exe->time, -1/*expansion*/, 
                    (int)exe->pool, exe->weighted_launches, exe->raw_launches,
                    exe->total_duration_sec, uri);
    write_string(wc->line);
    write_ln();

    g_free(uri);
}

static void
write_exemap(kp_exemap_t *exemap, kp_exe_t *exe, write_context_t *wc)
{
    write_tag(TAG_EXEMAP);
    g_string_printf(wc->line,
                    "%d\t%d\t%lg",
                    exe->seq, exemap->map->seq, exemap->prob);
    write_string(wc->line);
    write_ln();
}

/* Wrapper with correct GHFunc signature for write_exemap */
static void
write_exemap_wrapper(gpointer exemap, gpointer exe, gpointer user_data)
{
    write_exemap((kp_exemap_t *)exemap, (kp_exe_t *)exe, (write_context_t *)user_data);
}

/* Write markov (VERBATIM from upstream write_markov) */
static void
write_markov(kp_markov_t *markov, write_context_t *wc)
{
    int state, state_new;

    write_tag(TAG_MARKOV);
    g_string_printf(wc->line,
                    "%d\t%d\t%d",
                    markov->a->seq, markov->b->seq, markov->time);
    write_string(wc->line);

    for (state = 0; state < 4; state++) {
        g_string_printf(wc->line,
                        "\t%lg",
                        markov->time_to_leave[state]);
        write_string(wc->line);
    }
    for (state = 0; state < 4; state++) {
        for (state_new = 0; state_new < 4; state_new++) {
            g_string_printf(wc->line,
                            "\t%d",
                            markov->weight[state][state_new]);
            write_string(wc->line);
        }
    }

    write_ln();
}

/* Wrapper with correct GFunc signature for write_markov */
static void
write_markov_wrapper(gpointer data, gpointer user_data)
{
    write_markov((kp_markov_t *)data, (write_context_t *)user_data);
}

/* Write CRC32 footer for state file integrity */
static void
write_crc32(write_context_t *wc, int fd)
{
    uint32_t crc;
    off_t file_size;
    char *content;

    /* Get file size */
    file_size = lseek(fd, 0, SEEK_CUR);
    if (file_size <= 0) {
        return;  /* Can't calculate CRC, skip silently */
    }

    /* Read file contents to calculate CRC */
    content = g_malloc(file_size);
    if (!content) {
        g_warning("g_malloc failed for CRC calculation (%ld bytes) - skipping CRC footer",
                  (long)file_size);
        return;
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        g_free(content);
        return;
    }

    if (read(fd, content, file_size) != file_size) {
        g_free(content);
        /* Seek back to end */
        lseek(fd, 0, SEEK_END);
        return;
    }

    /* Calculate CRC32 */
    crc = kp_crc32(content, file_size);
    g_free(content);

    /* Seek back to end */
    lseek(fd, 0, SEEK_END);

    /* Write CRC32 footer */
    write_tag(TAG_CRC32);
    g_string_printf(wc->line, "%08X", crc);
    write_string(wc->line);
    write_ln();
}

/* Write family to state file (Enhancement #3) */
static void
write_family(gpointer key, kp_app_family_t *family, write_context_t *wc)
{
    (void)key;  /* family_id is already in family->family_id */
    
    g_return_if_fail(family);
    g_return_if_fail(family->member_paths);
    
    /* Format: FAMILY family_id method member1;member2;member3 */
    write_tag(TAG_FAMILY);
    
    /* Build semicolon-separated member list */
    GString *members = g_string_new("");
    for (guint i = 0; i < family->member_paths->len; i++) {
        if (i > 0) g_string_append_c(members, ';');
        g_string_append(members, g_ptr_array_index(family->member_paths, i));
    }
    
    g_string_printf(wc->line, "%s\t%d\t%s",
                    family->family_id,
                    (int)family->method,
                    members->str);
    
    write_string(wc->line);
    write_ln();
    
    g_string_free(members, TRUE);
}

/* Wrapper with correct GHFunc signature for write_family */
static void
write_family_wrapper(gpointer key, gpointer value, gpointer user_data)
{
    write_family(key, (kp_app_family_t *)value, (write_context_t *)user_data);
}

/* Write state to GIOChannel with CRC32 footer */
static char *
write_state(GIOChannel *f, int fd)
{
    write_context_t wc;

    wc.f = f;
    wc.line = g_string_sized_new(100);
    wc.err = NULL;

    write_header(&wc);
    if (!wc.err) g_hash_table_foreach   (kp_state->maps, (GHFunc)write_map, &wc);
    if (!wc.err) g_hash_table_foreach   (kp_state->bad_exes, write_badexe_wrapper, &wc);
    if (!wc.err) g_hash_table_foreach   (kp_state->exes, (GHFunc)write_exe, &wc);
    if (!wc.err) kp_exemap_foreach (write_exemap_wrapper, &wc);
    if (!wc.err) kp_markov_foreach (write_markov_wrapper, &wc);
    if (!wc.err) g_hash_table_foreach   (kp_state->app_families, write_family_wrapper, &wc);  /* Enhancement #3 */

    /* Flush before CRC calculation */
    if (!wc.err) {
        g_io_channel_flush(f, &wc.err);
    }

    /* Add CRC32 footer */
    if (!wc.err) {
        write_crc32(&wc, fd);
    }

    g_string_free(wc.line, TRUE);
    if (wc.err) {
        char *tmp;
        tmp = g_strdup(wc.err->message);
        g_error_free(wc.err);
        return tmp;
    } else
        return NULL;
}

/* Helper for removing all bad_exes: GHRFunc that always returns TRUE */
static gboolean
true_func(gpointer key, gpointer value, gpointer user_data)
{
    (void)key;
    (void)value;
    (void)user_data;
    return TRUE;
}

/**
 * Save state to file
 * Modified from upstream to add fsync for data durability
 */
void kp_state_save(const char *statefile)
{
    if (kp_state->dirty && statefile && *statefile) {
        int fd = -1;
        GIOChannel *f;
        char *tmpfile;

        g_message("saving state to %s", statefile);

        tmpfile = g_strconcat(statefile, ".tmp", NULL);
        g_debug("to be honest, saving state to %s", tmpfile);

        fd = open(tmpfile, O_RDWR | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
        if (fd < 0) {
            g_critical("cannot open %s for writing, ignoring: %s", tmpfile, strerror(errno));
        } else {
            char *errmsg;

            f = g_io_channel_unix_new(fd);

            errmsg = write_state(f, fd);
            g_io_channel_flush(f, NULL);
            g_io_channel_unref(f);

            if (errmsg) {
                g_critical("failed writing state to %s, ignoring: %s", tmpfile, errmsg);
                g_free(errmsg);
                close(fd);
                unlink(tmpfile);
            } else {
                /* fsync to ensure data durability before rename */
                if (fsync(fd) < 0) {
                    g_critical("fsync failed for %s: %s - state may be lost on crash",
                               tmpfile, strerror(errno));
                }
                close(fd);

                if (rename(tmpfile, statefile) < 0) {
                    g_critical("failed to rename %s to %s: %s",
                               tmpfile, statefile, strerror(errno));
                    unlink(tmpfile);
                } else {
                    g_debug("successfully renamed %s to %s", tmpfile, statefile);
                }
            }
        }

        g_free(tmpfile);

        kp_state->dirty = FALSE;

        g_debug("saving state done");
    }

    /* Clean up bad exes once in a while */
    g_hash_table_foreach_remove(kp_state->bad_exes, true_func, NULL);
}

/**
 * Free state memory
 * (VERBATIM from upstream preload_state_free)
 */
void kp_state_free(void)
{
    g_message("freeing state memory begin");
    g_hash_table_destroy(kp_state->bad_exes);
    kp_state->bad_exes = NULL;
    g_hash_table_destroy(kp_state->exes);
    kp_state->exes = NULL;

    /* Free family hash tables (Enhancement #3) */
    if (kp_state->app_families) {
        g_hash_table_destroy(kp_state->app_families);
        kp_state->app_families = NULL;
    }
    if (kp_state->exe_to_family) {
        g_hash_table_destroy(kp_state->exe_to_family);
        kp_state->exe_to_family = NULL;
    }

    g_assert(g_hash_table_size(kp_state->maps) == 0);
    g_assert(kp_state->maps_arr->len == 0);
    g_hash_table_destroy(kp_state->maps);
    kp_state->maps = NULL;
    g_slist_free(kp_state->running_exes);
    kp_state->running_exes = NULL;
    g_ptr_array_free(kp_state->maps_arr, TRUE);
    g_debug("freeing state memory done");
}

/**
 * Dump state to log
 * (VERBATIM from upstream preload_state_dump_log)
 */
void kp_state_dump_log(void)
{
    g_message("state log dump requested");
    fprintf(stderr, "persistent state stats:\n");
    fprintf(stderr, "preload time = %d\n", kp_state->time);
    fprintf(stderr, "num exes = %d\n", g_hash_table_size(kp_state->exes));
    fprintf(stderr, "num bad exes = %d\n", g_hash_table_size(kp_state->bad_exes));
    fprintf(stderr, "num maps = %d\n", g_hash_table_size(kp_state->maps));
    fprintf(stderr, "runtime state stats:\n");
    fprintf(stderr, "num running exes = %d\n", g_slist_length(kp_state->running_exes));
    g_debug("state log dump done");
}

/* ========================================================================
 * STATE PERIODIC TASKS - The Daemon's Heartbeat
 * ========================================================================
 *
 * The daemon operates on a periodic "tick" cycle (default 20 seconds):
 *
 * TIME 0:    kp_state_tick()   - Scan + Predict
 *              │
 *              ├─ kp_spy_scan() scans /proc for running processes
 *              │   └─ Updates running_exes list
 *              │   └─ Queues new executables for evaluation
 *              │
 *              └─ kp_prophet_predict() calculates what to preload
 *                  └─ Triggers readahead() system calls
 *
 * TIME T/2:  kp_state_tick2()  - Model Update
 *              │
 *              └─ kp_spy_update_model() evaluates queued exes
 *                  └─ Creates Markov chains for new apps
 *                  └─ Increments running time counters
 *
 * AUTOSAVE:
 *   Every autosave seconds (default 3600), kp_state_autosave() persists
 *   the learned model to disk for survival across daemon restarts.
 *
 * PAUSE SUPPORT:
 *   If kp_pause_is_active(), prediction/preloading is skipped to
 *   avoid I/O during user-specified quiet periods.
 *
 * SESSION BOOST:
 *   During session boot window, kp_session_preload_top_apps()
 *   aggressively preloads the most-used applications.
 *
 * ======================================================================== */

static gboolean kp_state_tick(gpointer data);

/* Tick2: Update model after half cycle delay (VERBATIM from upstream) */
static gboolean
kp_state_tick2(gpointer data)
{
    if (kp_state->model_dirty) {
        g_debug("state updating begin");
        kp_spy_update_model(data);
        kp_state->model_dirty = FALSE;
        g_debug("state updating end");
    }

    /* Increase time and reschedule */
    kp_state->time += (kp_conf->model.cycle + 1) / 2;
    g_timeout_add_seconds((kp_conf->model.cycle + 1) / 2, kp_state_tick, data);
    return FALSE;
}

/* Tick: Scan and predict (VERBATIM from upstream) */
static gboolean
kp_state_tick(gpointer data)
{
    if (kp_conf->system.doscan) {
        g_debug("state scanning begin");
        kp_spy_scan(data);
        kp_state->dirty = kp_state->model_dirty = TRUE;
        g_debug("state scanning end");
    }
    if (kp_conf->system.dopredict) {
        /* Check pause state before prediction/preloading */
        if (kp_pause_is_active()) {
            g_debug("preloading paused - skipping prediction");
        } else {
            /* Check for session start and boost if in boot window */
            kp_session_check();
            if (kp_session_in_boot_window()) {
                g_debug("session boot window active (%d sec remaining)",
                        kp_session_window_remaining());
                kp_session_preload_top_apps(5);  /* Boost top 5 apps */
            }

            g_debug("state predicting begin");
            kp_prophet_predict(data);
            g_debug("state predicting end");
        }
    }

    /* Increase time and reschedule */
    kp_state->time += kp_conf->model.cycle / 2;
    g_timeout_add_seconds(kp_conf->model.cycle / 2, kp_state_tick2, data);
    return FALSE;
}

static const char *autosave_statefile;

/* Autosave callback (VERBATIM from upstream) */
static gboolean
kp_state_autosave(gpointer user_data)
{
    (void)user_data;
    kp_state_save(autosave_statefile);

    g_timeout_add_seconds(kp_conf->system.autosave, kp_state_autosave, NULL);
    return FALSE;
}

/**
 * Start state periodic tasks
 * (VERBATIM from upstream preload_state_run)
 */
void kp_state_run(const char *statefile)
{
    g_timeout_add(0, kp_state_tick, NULL);
    if (statefile) {
        autosave_statefile = statefile;
        g_timeout_add_seconds(kp_conf->system.autosave, kp_state_autosave, NULL);
    }
}
