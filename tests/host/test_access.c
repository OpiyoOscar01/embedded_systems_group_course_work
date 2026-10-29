/**
 * @file test_access.c
 * @brief Host tests for the access decision table.
 * @author Opiyo Oscar
 * @requirements FR-ACC-01..09, FR-SCH-02, FR-SCH-03, FR-SCH-08
 */

#include "access_ctrl.h"
#include "test_util.h"

/** A valid, active tag scanning at an open, non-emergency building. */
static cocis_access_input_t valid_input(void)
{
    cocis_access_input_t input = {
        .user_known = true,
        .user_status = COCIS_USER_ACTIVE,
        .direction = COCIS_DIR_ENTRY,
        .schedule_state = COCIS_SCHED_OPEN,
        .emergency = false,
        .inside = false,
    };
    return input;
}

void test_access_identity(void)
{
    cocis_access_input_t input = valid_input();

    CHECK_EQ(cocis_access_evaluate(NULL).result, COCIS_RES_DENY_UNKNOWN);
    CHECK(!cocis_access_evaluate(NULL).open_door);

    input.user_known = false;
    CHECK_EQ(cocis_access_evaluate(&input).result, COCIS_RES_DENY_UNKNOWN);
    CHECK(!cocis_access_evaluate(&input).open_door);

    input = valid_input();
    input.user_status = COCIS_USER_SUSPENDED;
    CHECK_EQ(cocis_access_evaluate(&input).result, COCIS_RES_DENY_SUSPENDED);

    input.user_status = COCIS_USER_EXPIRED;
    CHECK_EQ(cocis_access_evaluate(&input).result, COCIS_RES_DENY_EXPIRED);
}

void test_access_emergency(void)
{
    cocis_access_input_t input = valid_input();
    input.emergency = true;

    /* Emergency passes even an unknown tag: the door is already released. */
    input.user_known = false;
    cocis_access_decision_t decision = cocis_access_evaluate(&input);
    CHECK_EQ(decision.result, COCIS_RES_EMERGENCY_PASS);
    CHECK(decision.open_door);
}

void test_access_schedule(void)
{
    cocis_access_input_t input = valid_input();
    input.schedule_state = COCIS_SCHED_CLOSED_HOURS;

    CHECK_EQ(cocis_access_evaluate(&input).result, COCIS_RES_DENY_OUTSIDE_HOURS);
    CHECK(!cocis_access_evaluate(&input).open_door);

    input.direction = COCIS_DIR_EXIT;
    CHECK_EQ(cocis_access_evaluate(&input).result, COCIS_RES_DENY_EXIT_OUTSIDE_HOURS);

    /* Holidays and unsynchronised time fail the same safe way. */
    input.direction = COCIS_DIR_ENTRY;
    input.schedule_state = COCIS_SCHED_CLOSED_HOLIDAY;
    CHECK_EQ(cocis_access_evaluate(&input).result, COCIS_RES_DENY_OUTSIDE_HOURS);
    input.schedule_state = COCIS_SCHED_CLOSED_NO_TIME;
    CHECK_EQ(cocis_access_evaluate(&input).result, COCIS_RES_DENY_OUTSIDE_HOURS);
}

void test_access_antipassback(void)
{
    cocis_access_input_t input = valid_input();

    /* Entry while already inside is denied. */
    input.inside = true;
    CHECK_EQ(cocis_access_evaluate(&input).result, COCIS_RES_DENY_ANTIPASSBACK);
    CHECK(!cocis_access_evaluate(&input).open_door);

    /* Exit while not inside is an anomaly but still opens the door. */
    input = valid_input();
    input.direction = COCIS_DIR_EXIT;
    input.inside = false;
    cocis_access_decision_t decision = cocis_access_evaluate(&input);
    CHECK_EQ(decision.result, COCIS_RES_ANOMALY_EXIT_NO_ENTRY);
    CHECK(decision.open_door);

    /* Valid entry and valid exit are granted. */
    input = valid_input();
    CHECK_EQ(cocis_access_evaluate(&input).result, COCIS_RES_GRANTED);
    input.direction = COCIS_DIR_EXIT;
    input.inside = true;
    decision = cocis_access_evaluate(&input);
    CHECK_EQ(decision.result, COCIS_RES_GRANTED);
    CHECK(decision.open_door);
}
