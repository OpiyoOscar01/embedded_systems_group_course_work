/**
 * @file occupancy.h
 * @brief Fixed-capacity occupancy table with anti-passback support.
 *
 * The table records who is currently inside the building. It drives the
 * building count, the roll-call list and the anti-passback rule. It is pure
 * C so the rules can be tested on the host; persistence lives elsewhere.
 *
 * @author Opiyo Oscar
 * @requirements FR-OCC-01, FR-OCC-03, FR-ACC-05, INN-10
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cocis_types.h"

#define COCIS_OCCUPANCY_MAX 64

/* One person currently inside. `since_utc` is used by the roll-call list. */
typedef struct {
    char uid[COCIS_UID_MAX];
    uint32_t since_utc;
} cocis_occupant_t;

/* The whole table. `version` bumps on every change so snapshots can detect
 * updates without comparing every entry. */
typedef struct {
    cocis_occupant_t inside[COCIS_OCCUPANCY_MAX];
    size_t count;
    uint32_t version;
} cocis_occupancy_t;

/** Clear the table. */
void cocis_occupancy_init(cocis_occupancy_t *occupancy);

/** True when the UID is currently marked as inside. */
bool cocis_occupancy_contains(const cocis_occupancy_t *occupancy, const char *uid);

/**
 * Record an entry. Fails when the UID is already inside (anti-passback) or
 * when the table is full.
 */
bool cocis_occupancy_enter(cocis_occupancy_t *occupancy, const char *uid, uint32_t since_utc);

/** Record an exit. Fails when the UID was not inside (exit anomaly). */
bool cocis_occupancy_leave(cocis_occupancy_t *occupancy, const char *uid);

/** Number of people currently inside. */
size_t cocis_occupancy_count(const cocis_occupancy_t *occupancy);

/**
 * Copy the occupant at `index` for the roll-call/console listing.
 * Returns NULL when the index is out of range.
 */
const cocis_occupant_t *cocis_occupancy_at(const cocis_occupancy_t *occupancy, size_t index);
