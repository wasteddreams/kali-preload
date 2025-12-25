/* readahead.h - Readahead stub for Preheat
 *
 * This is a temporary stub. Full implementation will be in Phase 7.
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef READAHEAD_H
#define READAHEAD_H

#include "../state/state.h"

/**
 * Perform readahead on array of maps
 * (Stub - will be implemented in Phase 7)
 *
 * @param maps Array of kp_map_t pointers
 * @param count Number of maps to readahead
 * @return Number of maps successfully read ahead
 */
int kp_readahead(kp_map_t **maps, int count);

#endif /* READAHEAD_H */
