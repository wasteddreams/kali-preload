/* confkeys.h - Configuration key definitions for Preheat
 *
 * Based on preload 0.6.4 confkeys.h
 * Copyright (C) 2025 Preheat Contributors
 *
 * =============================================================================
 * ARCHITECTURE: X-MACRO PATTERN FOR CONFIGURATION
 * =============================================================================
 *
 * This file uses the "X-Macro" pattern to define all configuration parameters
 * in a single, centralized location. The same definitions are then reused for:
 *
 *   1. Declaring struct fields in config.h (kp_conf_t structure)
 *   2. Setting default values in config.c (set_default_conf function)
 *   3. Loading values from INI file in config.c (kp_config_load function)
 *   4. Dumping values to log in config.c (kp_config_dump_log function)
 *
 * HOW IT WORKS:
 * -------------
 * This file is #included multiple times, each time with a different definition
 * of the confkey() macro. Each includer defines confkey() to extract what it
 * needs from the parameter list.
 *
 * MACRO SIGNATURE:
 *   confkey(group, type, key, default_value, unit)
 *
 *   @param group    Configuration section name (model, system, preheat)
 *   @param type     Data type (integer, boolean, enum, string, string_list)
 *   @param key      Configuration key name (becomes struct field name)
 *   @param default  Default value for this key
 *   @param unit     Unit multiplier (seconds, bytes, kilobytes) or - for none
 *
 * EXAMPLE:
 *   confkey(model, integer, cycle, 20, seconds)
 *
 *   This defines:
 *   - INI key: [model] section, "cycle" key
 *   - Default: 20 seconds
 *   - Struct field: kp_conf->model.cycle
 *
 * =============================================================================
 */

/* UPSTREAM PARAMETERS (must maintain compatibility with original preload) */

/* [model] section - Controls prediction algorithm behavior */

/* cycle: How often (in seconds) to scan /proc and update predictions.
 *        Smaller = more responsive but higher CPU usage. Range: 5-300 */
confkey(model,	integer,	cycle,		     20,	seconds)

/* usecorrelation: Use Markov chain correlation between applications.
 *                 When true, predicts apps based on what was launched before. */
confkey(model,	boolean,	usecorrelation,	   true,	-)

/* minsize: Minimum executable size (bytes) to consider for preloading.
 *          Helps avoid preloading tiny scripts/tools with no startup cost. */
confkey(model,	integer,	minsize,	2000000,	bytes)

/* memtotal/memfree/memcached: Memory thresholds as signed percentages.
 *   - Positive value = use that % of memory type
 *   - Negative value = reserve that % (don't use)
 *   Example: memtotal=-10 means "don't use more than 90% of total memory" */
confkey(model,	integer,	memtotal,	    -10,	signed_integer_percent)
confkey(model,	integer,	memfree,	     50,	signed_integer_percent)
confkey(model,	integer,	memcached,	      0,	signed_integer_percent)

/* [system] section - Controls daemon behavior and I/O strategy */

/* doscan: Enable /proc filesystem scanning to discover running processes */
confkey(system,	boolean,	doscan,		   true,	-)

/* dopredict: Enable prediction engine and readahead preloading */
confkey(system,	boolean,	dopredict,	   true,	-)

/* autosave: How often (seconds) to persist learned state to disk */
confkey(system,	integer,	autosave,	   3600,	seconds)

/* mapprefix: Semicolon-separated list of path prefixes to include/exclude.
 *            Prefix with ! to exclude. Example: "/usr;!/usr/share" */
confkey(system,	string_list,	mapprefix,	   NULL,	-)

/* exeprefix: Same as mapprefix but for executables specifically */
confkey(system,	string_list,	exeprefix,	   NULL,	-)

/* maxprocs: Max concurrent readahead operations (prevents I/O saturation) */
confkey(system,	integer,	maxprocs,	     30,	processes)

/* sortstrategy: How to order files for readahead to optimize disk seeks.
 *   0 = NONE   - No sorting, read in discovery order
 *   1 = PATH   - Sort alphabetically by path
 *   2 = INODE  - Sort by inode number (good for ext4)
 *   3 = BLOCK  - Sort by physical disk block (optimal, but needs root) */
confkey(system,	enum,		sortstrategy,	      3,	-)

/* manualapps: Path to file containing apps to always preload */
confkey(system,	string,		manualapps,	   NULL,	-)

/* excluded_patterns: Path patterns to exclude from priority pool (semicolon-separated).
 *                    Common system utilities that shouldn't clutter stats. */
confkey(system,	string,		excluded_patterns, "/bin/sh;/bin/bash;/usr/bin/grep;/usr/bin/cat;/usr/bin/sed;/usr/bin/awk;/usr/bin/find;/usr/bin/xargs;/sbin/",	-)

/* user_app_paths: Directories containing user applications (semicolon-separated).
 *                 Apps in these paths auto-promoted to priority pool. */
confkey(system,	string,		user_app_paths,	   "/usr/share/applications;/usr/local/share/applications;~/.local/share/applications;/opt",	-)

/* PREHEAT EXTENSIONS (opt-in, only active if --enable-preheat-extensions) */

#ifdef ENABLE_PREHEAT_EXTENSIONS
/* [preheat] section */
confkey(preheat,	boolean,	enable_preheat_scoring,	false,	-)
confkey(preheat,	integer,	preheat_tool_boost,	  100,	percent_times_100)
confkey(preheat,	boolean,	enable_time_learning,	false,	-)
confkey(preheat,	string,		manual_apps_list,	 NULL,	-)
confkey(preheat,	string,		blacklist,		 NULL,	-)

/* Weight calculation parameters (Enhancement #2) */
confkey(preheat,	integer,	weight_duration_divisor,  60,	seconds)
confkey(preheat,	integer,	weight_user_multiplier_x100, 200,	percent_times_100)

/* Seeding control (Enhancement #4) */
confkey(preheat,	boolean,	enable_seeding,		true,	-)
confkey(preheat,	boolean,	seed_xdg_recent,	true,	-)
confkey(preheat,	boolean,	seed_desktop_files,	true,	-)
confkey(preheat,	boolean,	seed_shell_history,	true,	-)
confkey(preheat,	boolean,	seed_browsers,		true,	-)
confkey(preheat,	boolean,	seed_dev_tools,		true,	-)
confkey(preheat,	boolean,	seed_system_patterns,	true,	-)
confkey(preheat,	integer,	browser_profile_days,	30,	days)
confkey(preheat,	integer,	dev_tools_access_days,	60,	days)
#endif
