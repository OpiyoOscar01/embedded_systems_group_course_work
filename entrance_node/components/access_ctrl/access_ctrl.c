/**
 * @file access_ctrl.c
 * @brief Access decision rules (pure logic, no hardware).
 * @author Opiyo Oscar
 * @requirements FR-ACC-01..09, FR-SCH-02, FR-SCH-03, FR-SCH-08
 */

#include "access_ctrl.h"

/** Build a decision in one place so the door flag stays consistent. */
static cocis_access_decision_t decide(cocis_result_t result, bool open_door)
{
    cocis_access_decision_t decision = {
        .result = result,
        .open_door = open_door,
    };
    return decision;
}

cocis_access_decision_t cocis_access_evaluate(const cocis_access_input_t *input)
{
    /* Defensive default: a missing input must never open the door. */
    if (input == NULL) {
        return decide(COCIS_RES_DENY_UNKNOWN, false);
    }

    /* 1. Life safety first: emergency mode releases both doors. */
    if (input->emergency) {
        return decide(COCIS_RES_EMERGENCY_PASS, true);
    }

    /* 2. Identity checks. */
    if (!input->user_known) {
        return decide(COCIS_RES_DENY_UNKNOWN, false);
    }
    if (input->user_status == COCIS_USER_SUSPENDED) {
        return decide(COCIS_RES_DENY_SUSPENDED, false);
    }
    if (input->user_status == COCIS_USER_EXPIRED) {
        return decide(COCIS_RES_DENY_EXPIRED, false);
    }

    /* 3. Opening hours. ENTRY and EXIT get different reason codes because the
     * brief treats them differently (FR-SCH-02 vs FR-SCH-03). */
    if (input->schedule_state != COCIS_SCHED_OPEN) {
        if (input->direction == COCIS_DIR_EXIT) {
            return decide(COCIS_RES_DENY_EXIT_OUTSIDE_HOURS, false);
        }
        return decide(COCIS_RES_DENY_OUTSIDE_HOURS, false);
    }

    /* 4. Anti-passback. Entry while already inside is a denial; exit without
     * a recorded entry is an anomaly but still opens the door, because
     * trapping somebody inside would be unsafe. */
    if (input->direction == COCIS_DIR_ENTRY && input->inside) {
        return decide(COCIS_RES_DENY_ANTIPASSBACK, false);
    }
    if (input->direction == COCIS_DIR_EXIT && !input->inside) {
        return decide(COCIS_RES_ANOMALY_EXIT_NO_ENTRY, true);
    }

    /* 5. Normal, valid scan. */
    return decide(COCIS_RES_GRANTED, true);
}
