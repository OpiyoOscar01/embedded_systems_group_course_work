/**
 * @file access_ctrl.h
 * @brief Pure access decision table for ENTRY and EXIT scans.
 *
 * The decision function is deliberately separated from hardware and storage:
 * given the tag, direction, schedule state and occupancy state it returns the
 * result code and whether the door should open. This makes the security rules
 * easy to review and to test exhaustively.
 *
 * @author Opiyo Oscar
 * @requirements FR-ACC-01..09, FR-SCH-02, FR-SCH-03, FR-SCH-08
 */

#pragma once

#include <stdbool.h>

#include "cocis_types.h"
#include "cocis_time.h"

/* Inputs needed to decide one scan. */
typedef struct {
    bool user_known;                     /* tag exists in the registry */
    cocis_user_status_t user_status;     /* effective status at today's date */
    cocis_dir_t direction;               /* ENTRY or EXIT */
    cocis_schedule_state_t schedule_state;
    bool emergency;                      /* emergency mode is active */
    bool inside;                         /* person currently inside */
} cocis_access_input_t;

/* Decision plus the side effect the door driver should apply. */
typedef struct {
    cocis_result_t result;
    bool open_door;
} cocis_access_decision_t;

/**
 * Evaluate one scan.
 *
 * Priority is intentional and follows the specification:
 *   1. emergency overrides everything (life safety)
 *   2. unknown / suspended / expired are denied
 *   3. outside schedule is denied (with the exit-specific reason)
 *   4. anti-passback on entry, exit anomaly on exit
 *   5. otherwise granted
 */
cocis_access_decision_t cocis_access_evaluate(const cocis_access_input_t *input);
