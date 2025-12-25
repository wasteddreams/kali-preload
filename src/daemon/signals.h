/* signals.h - Signal handling for Preheat
 *
 * Based on preload 0.6.4
 * Based on the preload daemon
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SIGNALS_H
#define SIGNALS_H

/**
 * Install signal handlers for daemon
 * Handles: SIGHUP, SIGUSR1, SIGUSR2, SIGTERM, SIGINT, SIGQUIT
 */
void kp_signals_init(void);

#endif /* SIGNALS_H */
