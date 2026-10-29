/**
 * @file occupancy.c
 * @brief Occupancy table operations (pure logic).
 * @author Opiyo Oscar
 * @requirements FR-OCC-01, FR-OCC-03, FR-ACC-05, INN-10
 */

#include "occupancy.h"

#include <string.h>

void cocis_occupancy_init(cocis_occupancy_t *occupancy)
{
    if (occupancy == NULL) {
        return;
    }
    memset(occupancy, 0, sizeof(*occupancy));
}

bool cocis_occupancy_contains(const cocis_occupancy_t *occupancy, const char *uid)
{
    if (occupancy == NULL || uid == NULL) {
        return false;
    }

    for (size_t i = 0; i < occupancy->count; i++) {
        if (strcmp(occupancy->inside[i].uid, uid) == 0) {
            return true;
        }
    }
    return false;
}

bool cocis_occupancy_enter(cocis_occupancy_t *occupancy, const char *uid, uint32_t since_utc)
{
    if (occupancy == NULL || uid == NULL || uid[0] == '\0') {
        return false;
    }
    if (cocis_occupancy_contains(occupancy, uid)) {
        return false;   /* anti-passback: already inside */
    }
    if (occupancy->count >= COCIS_OCCUPANCY_MAX) {
        return false;
    }

    cocis_occupant_t *slot = &occupancy->inside[occupancy->count];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->uid, uid, sizeof(slot->uid) - 1);
    slot->since_utc = since_utc;
    occupancy->count++;
    occupancy->version++;
    return true;
}

bool cocis_occupancy_leave(cocis_occupancy_t *occupancy, const char *uid)
{
    if (occupancy == NULL || uid == NULL) {
        return false;
    }

    for (size_t i = 0; i < occupancy->count; i++) {
        if (strcmp(occupancy->inside[i].uid, uid) != 0) {
            continue;
        }

        /* Move the last entry into the gap; order does not matter here. */
        occupancy->inside[i] = occupancy->inside[occupancy->count - 1];
        memset(&occupancy->inside[occupancy->count - 1], 0, sizeof(cocis_occupant_t));
        occupancy->count--;
        occupancy->version++;
        return true;
    }
    return false;   /* exit with no matching entry */
}

size_t cocis_occupancy_count(const cocis_occupancy_t *occupancy)
{
    return occupancy == NULL ? 0 : occupancy->count;
}

const cocis_occupant_t *cocis_occupancy_at(const cocis_occupancy_t *occupancy, size_t index)
{
    if (occupancy == NULL || index >= occupancy->count) {
        return NULL;
    }
    return &occupancy->inside[index];
}
