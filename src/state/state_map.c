/* state_map.c - Map and exemap management for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * =============================================================================
 * MODULE: Map and Exemap Management
 * =============================================================================
 *
 * Maps (kp_map_t) represent memory-mapped file regions:
 *
 *   map.path   = "/usr/lib/libc.so.6"
 *   map.offset = 0
 *   map.length = 1847296
 *
 * Maps are shared between executables via reference counting.
 *
 * Exemaps (kp_exemap_t) connect executables to the maps they use:
 *
 *   exe ── exemap ──> map
 *             │
 *             └── prob: probability this map is used when exe runs
 *
 * =============================================================================
 */

#include "common.h"
#include "state.h"
#include "state_map.h"

/* ========================================================================
 * MAP MANAGEMENT FUNCTIONS
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
