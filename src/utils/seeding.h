/* seeding.h - Smart first-run seeding for Preheat
 *
 * Copyright (C) 2025 Preheat Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SEEDING_H
#define SEEDING_H

/**
 * Seed initial state from user data sources
 * Called on first run when state file is missing or empty
 */
void kp_seed_from_sources(void);

#endif /* SEEDING_H */
