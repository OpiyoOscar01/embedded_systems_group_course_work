/**
 * @file test_door.c
 * @brief Host tests for the door lock timing state machine.
 * @author Opiyo Oscar
 * @requirements FR-ACC-03, FR-ACC-04
 */

#include "door_fsm.h"
#include "cocis_types.h"
#include "test_util.h"

void test_door_timing(void)
{
    cocis_door_fsm_t door;
    cocis_door_fsm_init(&door, 100);
    CHECK(!cocis_door_fsm_is_open(&door));

    CHECK(cocis_door_fsm_request(&door, 1000));
    CHECK(cocis_door_fsm_is_open(&door));

    /* Repeated requests do not extend the window. */
    CHECK(!cocis_door_fsm_request(&door, 1050));
    CHECK(!cocis_door_fsm_tick(&door, 1050));
    CHECK(!cocis_door_fsm_tick(&door, 1099));

    CHECK(cocis_door_fsm_tick(&door, 1100));
    CHECK(!cocis_door_fsm_is_open(&door));

    /* Ticks while locked are no-ops. */
    CHECK(!cocis_door_fsm_tick(&door, 1200));
    CHECK(cocis_door_fsm_request(&door, 1200));
}

void test_door_defaults_and_wrap(void)
{
    cocis_door_fsm_t door;
    cocis_door_fsm_init(&door, 0);
    CHECK_EQ(door.open_ms, COCIS_DOOR_OPEN_MS);

    /* 32-bit millisecond wrap: opened just before wrap, tick after it. */
    cocis_door_fsm_init(&door, 1000);
    CHECK(cocis_door_fsm_request(&door, 0xFFFFFF00u));
    CHECK(!cocis_door_fsm_tick(&door, 0x00000100u));   /* 512 ms elapsed */
    CHECK(cocis_door_fsm_tick(&door, 0x00000300u));    /* 1024 ms elapsed */

    /* NULL is handled safely. */
    CHECK(!cocis_door_fsm_request(NULL, 0));
    CHECK(!cocis_door_fsm_tick(NULL, 0));
    CHECK(!cocis_door_fsm_is_open(NULL));
}
