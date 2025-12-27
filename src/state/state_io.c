/* state_io.c - State file I/O for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * =============================================================================
 * MODULE: State File I/O
 * =============================================================================
 *
 * This module handles reading and writing the persistent state file.
 *
 * READ SEQUENCE:
 *   1. read_map()     - Memory map regions
 *   2. read_badexe()  - Blacklisted executables (skipped)
 *   3. read_exe()     - Tracked executables
 *   4. read_exemap()  - Exe-to-map associations
 *   5. read_markov()  - Correlation chains
 *   6. read_family()  - Application families
 *   7. read_crc32()   - Integrity verification
 *
 * WRITE SEQUENCE:
 *   1. write_header() - Version info
 *   2. write_map()    - All maps
 *   3. write_badexe() - Blacklisted exes
 *   4. write_exe()    - All exes
 *   5. write_exemap() - All exemaps
 *   6. write_markov() - All Markov chains
 *   7. write_family() - All families
 *   8. write_crc32()  - CRC32 footer
 *
 * =============================================================================
 */

#include "common.h"
#include "../utils/logging.h"
#include "../utils/crc32.h"
#include "../config/config.h"
#include "../monitor/proc.h"
#include "state.h"
#include "state_io.h"

#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* ========================================================================
 * STATE FILE FORMAT TAGS
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

/* ========================================================================
 * READ CONTEXT
 * ======================================================================== */

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

/* ========================================================================
 * READ FUNCTIONS
 * ======================================================================== */

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
            goto err;
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
                goto err;
            }

            rc->line += n;
            markov->weight[state][state_new] = x;
        }
    }
    return;

err:
    kp_markov_free(markov, NULL);
}

/* Read CRC32 footer from state file */
static void
read_crc32(read_context_t *rc)
{
    unsigned int stored_crc;
    
    if (1 > sscanf(rc->line, "%x", &stored_crc)) {
        g_debug("CRC32 line malformed, ignoring");
    }
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
    
    member_token = strtok(members_str, ";");
    while (member_token) {
        while (*member_token == ' ') member_token++;
        if (*member_token) {
            kp_family_add_member(family, member_token);
        }
        member_token = strtok(NULL, ";");
    }
    
    g_hash_table_insert(kp_state->app_families, g_strdup(family_id), family);
}

/* Helper callbacks for state initialization */
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

static void
set_running_process_callback_wrapper(gpointer key, gpointer value, gpointer user_data)
{
    (void)key;
    set_running_process_callback(0, (const char *)value, GPOINTER_TO_INT(user_data));
}

static void
set_markov_state_callback_wrapper(gpointer data, gpointer user_data)
{
    (void)user_data;
    set_markov_state_callback((kp_markov_t *)data);
}

/* Read state from GIOChannel */
char *
kp_state_read_from_channel(GIOChannel *f)
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

        if (1 > sscanf(rc.line, "%31s", tag)) {
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
        else if (!strcmp(tag, TAG_FAMILY)) read_family(&rc);
        else if (!strcmp(tag, TAG_CRC32))  read_crc32(&rc);
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

/* ========================================================================
 * WRITE CONTEXT AND MACROS
 * ======================================================================== */

typedef struct _write_context_t
{
    GIOChannel *f;
    GString *line;
    GError *err;
} write_context_t;

#define write_it(s) \
    if (wc->err || G_IO_STATUS_NORMAL != g_io_channel_write_chars(wc->f, s, -1, NULL, &(wc->err))) \
        return;
#define write_tag(tag) write_it(tag "\t")
#define write_string(string) write_it((string)->str)
#define write_ln() write_it("\n")

/* ========================================================================
 * WRITE FUNCTIONS
 * ======================================================================== */

static void
write_header(write_context_t *wc)
{
    write_tag(TAG_PRELOAD);
    g_string_printf(wc->line, "%s\t%d", VERSION, kp_state->time);
    write_string(wc->line);
    write_ln();
}

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
                    map->seq, map->update_time, (long)map->offset, (long)map->length, -1, uri);
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
    g_string_printf(wc->line, "%d\t%d\t%s", update_time, -1, uri);
    write_string(wc->line);
    write_ln();

    g_free(uri);
}

static void
write_badexe_wrapper(gpointer key, gpointer value, gpointer user_data)
{
    write_badexe((char *)key, GPOINTER_TO_INT(value), (write_context_t *)user_data);
}

static void
write_exe(gpointer G_GNUC_UNUSED key, kp_exe_t *exe, write_context_t *wc)
{
    char *uri;

    uri = g_filename_to_uri(exe->path, NULL, &(wc->err));
    if (!uri)
        return;

    write_tag(TAG_EXE);
    g_string_printf(wc->line,
                    "%d\t%d\t%d\t%d\t%d\t%.6f\t%lu\t%lu\t%s",
                    exe->seq, exe->update_time, exe->time, -1, 
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
    g_string_printf(wc->line, "%d\t%d\t%lg", exe->seq, exemap->map->seq, exemap->prob);
    write_string(wc->line);
    write_ln();
}

static void
write_exemap_wrapper(gpointer exemap, gpointer exe, gpointer user_data)
{
    write_exemap((kp_exemap_t *)exemap, (kp_exe_t *)exe, (write_context_t *)user_data);
}

static void
write_markov(kp_markov_t *markov, write_context_t *wc)
{
    int state, state_new;

    write_tag(TAG_MARKOV);
    g_string_printf(wc->line, "%d\t%d\t%d", markov->a->seq, markov->b->seq, markov->time);
    write_string(wc->line);

    for (state = 0; state < 4; state++) {
        g_string_printf(wc->line, "\t%lg", markov->time_to_leave[state]);
        write_string(wc->line);
    }
    for (state = 0; state < 4; state++) {
        for (state_new = 0; state_new < 4; state_new++) {
            g_string_printf(wc->line, "\t%d", markov->weight[state][state_new]);
            write_string(wc->line);
        }
    }

    write_ln();
}

static void
write_markov_wrapper(gpointer data, gpointer user_data)
{
    write_markov((kp_markov_t *)data, (write_context_t *)user_data);
}

static void
write_crc32(write_context_t *wc, int fd)
{
    uint32_t crc;
    off_t file_size;
    char *content;

    file_size = lseek(fd, 0, SEEK_CUR);
    if (file_size <= 0)
        return;

    content = g_malloc(file_size);
    if (!content) {
        g_warning("g_malloc failed for CRC calculation (%ld bytes)", (long)file_size);
        return;
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        g_free(content);
        return;
    }

    if (read(fd, content, file_size) != file_size) {
        g_free(content);
        lseek(fd, 0, SEEK_END);
        return;
    }

    crc = kp_crc32(content, file_size);
    g_free(content);

    lseek(fd, 0, SEEK_END);

    write_tag(TAG_CRC32);
    g_string_printf(wc->line, "%08X", crc);
    write_string(wc->line);
    write_ln();
}

static void
write_family(gpointer key, kp_app_family_t *family, write_context_t *wc)
{
    (void)key;
    
    g_return_if_fail(family);
    g_return_if_fail(family->member_paths);
    
    write_tag(TAG_FAMILY);
    
    GString *members = g_string_new("");
    for (guint i = 0; i < family->member_paths->len; i++) {
        if (i > 0) g_string_append_c(members, ';');
        g_string_append(members, g_ptr_array_index(family->member_paths, i));
    }
    
    g_string_printf(wc->line, "%s\t%d\t%s",
                    family->family_id, (int)family->method, members->str);
    
    write_string(wc->line);
    write_ln();
    
    g_string_free(members, TRUE);
}

static void
write_family_wrapper(gpointer key, gpointer value, gpointer user_data)
{
    write_family(key, (kp_app_family_t *)value, (write_context_t *)user_data);
}

/* Write state to GIOChannel with CRC32 footer */
char *
kp_state_write_to_channel(GIOChannel *f, int fd)
{
    write_context_t wc;

    wc.f = f;
    wc.line = g_string_sized_new(100);
    wc.err = NULL;

    write_header(&wc);
    if (!wc.err) g_hash_table_foreach(kp_state->maps, (GHFunc)write_map, &wc);
    if (!wc.err) g_hash_table_foreach(kp_state->bad_exes, write_badexe_wrapper, &wc);
    if (!wc.err) g_hash_table_foreach(kp_state->exes, (GHFunc)write_exe, &wc);
    if (!wc.err) kp_exemap_foreach(write_exemap_wrapper, &wc);
    if (!wc.err) kp_markov_foreach(write_markov_wrapper, &wc);
    if (!wc.err) g_hash_table_foreach(kp_state->app_families, write_family_wrapper, &wc);

    if (!wc.err) {
        g_io_channel_flush(f, &wc.err);
    }

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

/* Handle corrupt state file by renaming it */
gboolean
kp_state_handle_corrupt_file(const char *statefile, const char *reason)
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
    return TRUE;
}
