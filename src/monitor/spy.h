/* spy.h - Process tracking header for Preheat
 *
 * Based on preload 0.6.4 spy.h
 * Based on the preload daemon
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SPY_H
#define SPY_H

/**
 * Scan running processes
 * (VERBATIM signature from upstream preload_spy_scan)
 */
void kp_spy_scan(gpointer data);

/**
 * Update prediction model
 * (VERBATIM signature from upstream preload_spy_update_model)
 */
void kp_spy_update_model(gpointer data);

#endif /* SPY_H */
