/* prophet.h - Prediction engine for Preheat
 *
 * Based on preload 0.6.4 prophet.h
 * Based on the preload daemon
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef PROPHET_H
#define PROPHET_H

#include <glib.h>

/**
 * Predict which maps should be preloaded
 * (VERBATIM signature from upstream preload_prophet_predict)
 */
void kp_prophet_predict(gpointer data);

/**
 * Perform readahead based on memory budget
 * (VERBATIM signature from upstream preload_prophet_readahead)
 */
void kp_prophet_readahead(GPtrArray *maps_arr);

#endif /* PROPHET_H */
