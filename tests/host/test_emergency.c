/**
 * @file test_emergency.c
 * @brief Host tests for emergency mode and the roll-call snapshot.
 * @author Opiyo Oscar
 * @requirements FR-EMG-01..04, INN-02, INN-10
 */

#include "emergency.h"
#include "test_util.h"

#include <string.h>

void test_emergency_state(void)
{
    cocis_emergency_t emergency;
    cocis_emergency_init(&emergency);
    CHECK(!cocis_emergency_is_active(&emergency));

    cocis_emergency_activate(&emergency, 1234, "BUTTON");
    CHECK(cocis_emergency_is_active(&emergency));
    CHECK_EQ(emergency.since_utc, 1234);
    CHECK(strcmp(emergency.source, "BUTTON") == 0);

    cocis_emergency_clear(&emergency);
    CHECK(!cocis_emergency_is_active(&emergency));
    CHECK_EQ(emergency.since_utc, 0);

    /* Long/clipped sources are handled safely. */
    cocis_emergency_activate(&emergency, 1, "VERYLONGSOURCE");
    CHECK(strcmp(emergency.source, "VERYLON") == 0);

    cocis_emergency_activate(&emergency, 1, NULL);
    CHECK(strcmp(emergency.source, "UNKNOWN") == 0);

    CHECK(!cocis_emergency_is_active(NULL));
}

void test_emergency_roll_call(void)
{
    cocis_occupancy_t occ;
    cocis_occupancy_init(&occ);
    CHECK(cocis_occupancy_enter(&occ, "A1", 100));
    CHECK(cocis_occupancy_enter(&occ, "B2", 200));

    cocis_roll_call_t roll;
    cocis_roll_call_capture(&roll, &occ, 5000);
    CHECK_EQ(roll.captured_utc, 5000);
    CHECK_EQ(cocis_roll_call_count(&roll), 2);

    const cocis_occupant_t *first = cocis_roll_call_at(&roll, 0);
    const cocis_occupant_t *second = cocis_roll_call_at(&roll, 1);
    CHECK(first != NULL && second != NULL);
    CHECK_EQ(first->since_utc, 100);
    CHECK_EQ(second->since_utc, 200);
    CHECK(cocis_roll_call_at(&roll, 2) == NULL);

    /* A later capture reflects the new table, not the old snapshot. */
    CHECK(cocis_occupancy_leave(&occ, "A1"));
    cocis_roll_call_capture(&roll, &occ, 6000);
    CHECK_EQ(cocis_roll_call_count(&roll), 1);
    CHECK(strcmp(cocis_roll_call_at(&roll, 0)->uid, "B2") == 0);

    cocis_roll_call_capture(&roll, NULL, 7000);
    CHECK_EQ(cocis_roll_call_count(&roll), 0);
    CHECK_EQ(cocis_roll_call_count(NULL), 0);
    CHECK(cocis_roll_call_at(NULL, 0) == NULL);
}
