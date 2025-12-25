/* blacklist.c - Blacklist implementation for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * =============================================================================
 * MODULE OVERVIEW: Application Blacklist
 * =============================================================================
 *
 * This module manages a list of applications that should NEVER be preloaded,
 * even if the prediction engine suggests them. This is useful for:
 *
 *   - Applications that shouldn't be read into memory (security tools)
 *   - Apps that cause issues when preloaded
 *   - User preference to exclude specific programs
 *
 * FILE LOCATION:
 *   /etc/preheat.d/blacklist (or SYSCONFDIR/preheat.d/blacklist)
 *
 * FILE FORMAT:
 *   - One binary name per line (NOT full paths, just the executable name)
 *   - Lines starting with # are comments
 *   - Valid characters: alphanumeric, underscore, dash, dot
 *
 * EXAMPLE BLACKLIST:
 *   # Don't preload security tools
 *   wireshark
 *   nmap
 *   # Don't preload large IDEs
 *   eclipse
 *
 * RELOAD SUPPORT:
 *   kp_blacklist_reload() can be called on SIGHUP to reload without restart.
 *   Uses mtime checking to skip unnecessary reloads.
 *
 * =============================================================================
 */

#include "common.h"
#include "blacklist.h"
#include "../utils/logging.h"

#include <sys/stat.h>
#include <libgen.h>

/* Default blacklist file location (set at compile time) */
#define BLACKLIST_DIR "/etc/preheat.d"
#define BLACKLIST_FILE BLACKLIST_DIR "/blacklist"

/* Maximum line length in blacklist file */
#define BLACKLIST_LINE_MAX 256

/*
 * Global blacklist state.
 * 
 * Uses a hash table for O(1) lookup performance since this is called
 * for every process discovered during /proc scanning.
 */
static struct {
    GHashTable *entries;      /* Binary name -> TRUE (hash set pattern) */
    char *filepath;           /* Path to blacklist file */
    time_t last_modified;     /* File mtime for change detection on reload */
    int count;                /* Number of entries (cached for quick access) */
} blacklist = {0};

/**
 * Parse blacklist file and populate hash table
 */
static void
load_blacklist_file(const char *filepath)
{
    FILE *fp;
    char line[BLACKLIST_LINE_MAX];
    int loaded = 0;
    int skipped = 0;
    struct stat st;

    /* Clear existing entries */
    if (blacklist.entries) {
        g_hash_table_remove_all(blacklist.entries);
    } else {
        blacklist.entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
    blacklist.count = 0;

    /* Check if file exists */
    if (stat(filepath, &st) < 0) {
        if (errno == ENOENT) {
            g_debug("Blacklist file not found: %s (this is normal)", filepath);
        } else {
            g_warning("Cannot stat blacklist file %s: %s", filepath, strerror(errno));
        }
        return;
    }

    blacklist.last_modified = st.st_mtime;

    fp = fopen(filepath, "r");
    if (!fp) {
        g_warning("Cannot open blacklist file %s: %s", filepath, strerror(errno));
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        char *end;

        /* Skip leading whitespace */
        while (*p && g_ascii_isspace(*p)) p++;

        /* Skip empty lines and comments */
        if (!*p || *p == '#' || *p == '\n') continue;

        /* Remove trailing newline/whitespace */
        end = p + strlen(p) - 1;
        while (end > p && g_ascii_isspace(*end)) *end-- = '\0';

        /* Check line length */
        if (strlen(p) >= BLACKLIST_LINE_MAX - 1) {
            g_warning("Blacklist entry too long, skipping: %.50s...", p);
            skipped++;
            continue;
        }

        /* Validate entry: alphanumeric, underscore, dash, dot only */
        gboolean valid = TRUE;
        for (char *c = p; *c; c++) {
            if (!g_ascii_isalnum(*c) && *c != '_' && *c != '-' && *c != '.') {
                valid = FALSE;
                break;
            }
        }

        if (!valid) {
            g_warning("Invalid blacklist entry (bad characters), skipping: %s", p);
            skipped++;
            continue;
        }

        /* Add to hash table */
        g_hash_table_insert(blacklist.entries, g_strdup(p), GINT_TO_POINTER(1));
        loaded++;
    }

    fclose(fp);

    blacklist.count = loaded;

    if (loaded > 0 || skipped > 0) {
        g_message("Blacklist loaded: %d entries from %s%s",
                  loaded, filepath,
                  skipped > 0 ? g_strdup_printf(" (%d skipped)", skipped) : "");
    }
}

/**
 * Initialize the blacklist subsystem
 */
void
kp_blacklist_init(void)
{
    g_debug("Initializing blacklist subsystem");

    /* Store filepath */
    blacklist.filepath = g_strdup(BLACKLIST_FILE);

    /* Load initial blacklist */
    load_blacklist_file(blacklist.filepath);
}

/**
 * Reload blacklist from file
 */
void
kp_blacklist_reload(void)
{
    struct stat st;

    if (!blacklist.filepath) {
        kp_blacklist_init();
        return;
    }

    /* Check if file has changed */
    if (stat(blacklist.filepath, &st) == 0) {
        if (st.st_mtime == blacklist.last_modified) {
            g_debug("Blacklist file unchanged, skipping reload");
            return;
        }
    }

    g_message("Reloading blacklist from %s", blacklist.filepath);
    load_blacklist_file(blacklist.filepath);
}

/**
 * Check if a binary is blacklisted
 */
gboolean
kp_blacklist_contains(const char *binary_name)
{
    char *base;
    char *path_copy;
    gboolean result;

    if (!blacklist.entries || !binary_name) {
        return FALSE;
    }

    /* Extract basename if full path given */
    if (binary_name[0] == '/') {
        path_copy = g_strdup(binary_name);
        base = basename(path_copy);
        result = g_hash_table_contains(blacklist.entries, base);
        g_free(path_copy);
    } else {
        result = g_hash_table_contains(blacklist.entries, binary_name);
    }

    return result;
}

/**
 * Get count of blacklisted entries
 */
int
kp_blacklist_count(void)
{
    return blacklist.count;
}

/**
 * Free blacklist resources
 */
void
kp_blacklist_free(void)
{
    if (blacklist.entries) {
        g_hash_table_destroy(blacklist.entries);
        blacklist.entries = NULL;
    }

    g_free(blacklist.filepath);
    blacklist.filepath = NULL;
    blacklist.count = 0;
}
