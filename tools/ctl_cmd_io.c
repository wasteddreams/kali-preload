/* ctl_cmd_io.c - Import/export commands
 *
 * Copyright (C) 2025 Preheat Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Commands: export, import
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "ctl_commands.h"

/* File paths */
#define STATEFILE "/usr/local/var/lib/preheat/preheat.state"
#define DEFAULT_EXPORT "preheat-profile.json"

/**
 * Command: export - Export learned patterns to JSON
 */
int
cmd_export(const char *filepath)
{
    FILE *state_f, *export_f;
    char line[1024];
    const char *outpath = filepath ? filepath : DEFAULT_EXPORT;
    time_t now = time(NULL);
    int apps_exported = 0;

    state_f = fopen(STATEFILE, "r");
    if (!state_f) {
        if (errno == EACCES || errno == EPERM) {
            fprintf(stderr, "Error: Permission denied reading state file\n");
            fprintf(stderr, "Hint: Try with sudo\n");
        } else {
            fprintf(stderr, "Error: Cannot open state file %s: %s\n", STATEFILE, strerror(errno));
        }
        return 1;
    }

    export_f = fopen(outpath, "w");
    if (!export_f) {
        fprintf(stderr, "Error: Cannot create export file %s: %s\n", outpath, strerror(errno));
        fclose(state_f);
        return 1;
    }

    fprintf(export_f, "{\n");
    fprintf(export_f, "  \"preheat_export_version\": \"1.0\",\n");
    fprintf(export_f, "  \"exported_at\": %ld,\n", now);
    fprintf(export_f, "  \"apps\": [\n");

    int first = 1;
    while (fgets(line, sizeof(line), state_f)) {
        if (strncmp(line, "EXE\t", 4) == 0) {
            int seq, update_time, run_time, expansion;
            char path[512];

            if (sscanf(line, "EXE\t%d\t%d\t%d\t%d\t%511s",
                       &seq, &update_time, &run_time, &expansion, path) >= 5) {
                if (!first) fprintf(export_f, ",\n");
                fprintf(export_f, "    {\"path\": \"%s\", \"run_time\": %d}", path, run_time);
                apps_exported++;
                first = 0;
            }
        }
    }

    fprintf(export_f, "\n  ]\n");
    fprintf(export_f, "}\n");

    fclose(state_f);
    fclose(export_f);

    printf("Exported %d apps to %s\n", apps_exported, outpath);
    return 0;
}

/**
 * Command: import - Validate JSON import file
 */
int
cmd_import(const char *filepath)
{
    FILE *f;
    char line[2048];
    const char *inpath = filepath ? filepath : DEFAULT_EXPORT;
    int apps_found = 0;

    f = fopen(inpath, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open import file %s: %s\n", inpath, strerror(errno));
        return 1;
    }

    int valid = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "preheat_export_version")) valid = 1;
        if (strstr(line, "\"path\"")) apps_found++;
    }
    fclose(f);

    if (!valid) {
        fprintf(stderr, "Error: Invalid export file format\n");
        return 1;
    }

    printf("Found %d apps in %s\n", apps_found, inpath);
    printf("\nNote: Import currently validates the file only.\n");
    printf("To apply: copy the apps to your whitelist file at:\n");
    printf("  /etc/preheat.d/apps.list\n");
    printf("Then run: sudo preheat-ctl reload\n");

    return 0;
}
