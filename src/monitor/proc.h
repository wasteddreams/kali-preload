#ifndef PROC_H
#define PROC_H

#include <sys/types.h>
#include <glib.h>

/* GSet compatibility macros */
#define GSet                GPtrArray
#define g_set_new()         g_ptr_array_new()
#define g_set_add(s,v)      g_ptr_array_add(s, v)
#define g_set_remove(s,v)   g_ptr_array_remove_fast(s, v)
#define g_set_free(s)       g_ptr_array_free(s, TRUE)
#define g_set_size(s)       ((s)->len)
#define g_set_foreach       g_ptr_array_foreach

/**
 * kp_memory_t: System memory information
 * (VERBATIM from upstream preload_memory_t)
 */
typedef struct _kp_memory_t
{
    /* All values in kilobytes (1024) */

    int total;      /* Total memory */
    int free;       /* Free memory */
    int buffers;    /* Buffers memory */
    int cached;     /* Page-cache memory */

    int pagein;     /* Total data paged (read) in since boot */
    int pageout;    /* Total data paged (written) out since boot */

} kp_memory_t;

/**
 * Read system memory information from /proc/meminfo and /proc/vmstat
 * (VERBATIM signature from upstream)
 */
void kp_proc_get_memstat(kp_memory_t *mem);

/**
 * Get memory maps for a process
 * Returns sum of length of maps in bytes, or 0 if failed
 * (VERBATIM signature from upstream)
 *
 * @param pid Process ID to scan
 * @param maps Hash table to populate with maps (can be NULL)
 * @param exemaps Set to populate with exemaps (can be NULL, output parameter)
 * @return Total size of all maps in bytes
 */
size_t kp_proc_get_maps(pid_t pid, GHashTable *maps, GSet **exemaps);

/**
 * Iterate over all running processes
 * (VERBATIM signature from upstream)
 *
 * @param func Callback function (pid as key, exe path as value)
 * @param user_data Data to pass to callback
 */
void kp_proc_foreach(GHFunc func, gpointer user_data);

#endif /* PROC_H */
