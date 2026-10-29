/**
 * @file emergency.c
 * @brief Emergency mode and roll-call snapshot (pure logic).
 * @author Opiyo Oscar
 * @requirements FR-EMG-01..04, INN-02, INN-10
 */

#include "emergency.h"

#include <stdio.h>
#include <string.h>

void cocis_emergency_init(cocis_emergency_t *emergency)
{
    if (emergency == NULL) {
        return;
    }
    memset(emergency, 0, sizeof(*emergency));
}

void cocis_emergency_activate(cocis_emergency_t *emergency, uint32_t since_utc,
                              const char *source)
{
    if (emergency == NULL) {
        return;
    }

    emergency->active = true;
    emergency->since_utc = since_utc;
    snprintf(emergency->source, sizeof(emergency->source), "%s",
             (source == NULL) ? "UNKNOWN" : source);
}

void cocis_emergency_clear(cocis_emergency_t *emergency)
{
    if (emergency == NULL) {
        return;
    }
    emergency->active = false;
    emergency->since_utc = 0;
    emergency->source[0] = '\0';
}

bool cocis_emergency_is_active(const cocis_emergency_t *emergency)
{
    return emergency != NULL && emergency->active;
}

void cocis_roll_call_capture(cocis_roll_call_t *roll_call,
                             const cocis_occupancy_t *occupancy,
                             uint32_t now_utc)
{
    if (roll_call == NULL) {
        return;
    }

    memset(roll_call, 0, sizeof(*roll_call));
    roll_call->captured_utc = now_utc;

    if (occupancy == NULL) {
        return;
    }

    size_t count = cocis_occupancy_count(occupancy);
    if (count > COCIS_OCCUPANCY_MAX) {
        count = COCIS_OCCUPANCY_MAX;
    }

    for (size_t i = 0; i < count; i++) {
        const cocis_occupant_t *person = cocis_occupancy_at(occupancy, i);
        if (person != NULL) {
            roll_call->people[roll_call->count] = *person;
            roll_call->count++;
        }
    }
}

size_t cocis_roll_call_count(const cocis_roll_call_t *roll_call)
{
    return roll_call == NULL ? 0 : roll_call->count;
}

const cocis_occupant_t *cocis_roll_call_at(const cocis_roll_call_t *roll_call, size_t index)
{
    if (roll_call == NULL || index >= roll_call->count) {
        return NULL;
    }
    return &roll_call->people[index];
}
