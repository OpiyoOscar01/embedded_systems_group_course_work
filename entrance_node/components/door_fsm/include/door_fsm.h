/**
 * @file door_fsm.h
 * @brief Door lock timing state machine.
 *
 * The servo is modelled as a two-state machine: LOCKED and OPEN. A granted
 * scan requests OPEN; after COCIS_DOOR_OPEN_MS the caller's tick closes it
 * again. Timing uses a caller-supplied millisecond clock, which keeps the
 * module testable and independent of FreeRTOS.
 *
 * @author Opiyo Oscar
 * @requirements FR-ACC-03, FR-ACC-04
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool open;
    uint32_t opened_at_ms;
    uint32_t open_ms;
} cocis_door_fsm_t;

/** Initialise the FSM in the LOCKED state. A zero duration selects the default. */
void cocis_door_fsm_init(cocis_door_fsm_t *door, uint32_t open_ms);

/**
 * Request an unlock. Returns false when the door is already open, so repeated
 * scans do not extend or re-trigger the unlock window.
 */
bool cocis_door_fsm_request(cocis_door_fsm_t *door, uint32_t now_ms);

/**
 * Advance the FSM. Returns true exactly on the OPEN -> LOCKED transition so
 * the caller can log/refresh the display once.
 */
bool cocis_door_fsm_tick(cocis_door_fsm_t *door, uint32_t now_ms);

/** True while the lock is released. */
bool cocis_door_fsm_is_open(const cocis_door_fsm_t *door);
