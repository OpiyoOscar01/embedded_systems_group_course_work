/**
 * @file emergency.h
 * @brief Emergency mode state and roll-call snapshot.
 *
 * Emergency mode is driven by the entrance button (or the CLI). It unlocks
 * both doors and freezes the roll-call list at the moment of activation so
 * security can report who was inside. The module is pure logic; the ISR and
 * GPIO glue live in the HAL/main.
 *
 * @author Opiyo Oscar
 * @requirements FR-EMG-01..04, INN-02, INN-10
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cocis_types.h"
#include "occupancy.h"

typedef struct {
    bool active;
    uint32_t since_utc;
    char source[8];      /* "BUTTON" or "CLI" */
} cocis_emergency_t;

/* Frozen list of occupants captured at activation time. */
typedef struct {
    cocis_occupant_t people[COCIS_OCCUPANCY_MAX];
    size_t count;
    uint32_t captured_utc;
} cocis_roll_call_t;

/** Reset to normal mode with an empty roll-call list. */
void cocis_emergency_init(cocis_emergency_t *emergency);

/** Enter emergency mode; `source` is copied (max 7 characters). */
void cocis_emergency_activate(cocis_emergency_t *emergency, uint32_t since_utc,
                              const char *source);

/** Leave emergency mode. */
void cocis_emergency_clear(cocis_emergency_t *emergency);

/** True while emergency mode is active. */
bool cocis_emergency_is_active(const cocis_emergency_t *emergency);

/**
 * Snapshot the current occupancy table into the roll-call list.
 * Called once when emergency mode is activated.
 */
void cocis_roll_call_capture(cocis_roll_call_t *roll_call,
                             const cocis_occupancy_t *occupancy,
                             uint32_t now_utc);

/** Number of people in the frozen list. */
size_t cocis_roll_call_count(const cocis_roll_call_t *roll_call);

/** Person at `index`, or NULL. */
const cocis_occupant_t *cocis_roll_call_at(const cocis_roll_call_t *roll_call, size_t index);
