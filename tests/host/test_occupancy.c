/**
 * @file test_occupancy.c
 * @brief Host tests for the occupancy table and anti-passback bookkeeping.
 * @author Opiyo Oscar
 * @requirements FR-OCC-01, FR-OCC-03, FR-ACC-05
 */

#include "occupancy.h"
#include "test_util.h"

void test_occupancy_flow(void)
{
    cocis_occupancy_t occ;
    cocis_occupancy_init(&occ);

    CHECK_EQ(cocis_occupancy_count(&occ), 0);
    CHECK_EQ(occ.version, 0);
    CHECK(!cocis_occupancy_contains(&occ, "A1"));
    CHECK(cocis_occupancy_at(&occ, 0) == NULL);

    CHECK(cocis_occupancy_enter(&occ, "A1", 1000));
    CHECK(cocis_occupancy_contains(&occ, "A1"));
    CHECK_EQ(cocis_occupancy_count(&occ), 1);
    CHECK_EQ(occ.version, 1);

    /* Anti-passback: the same tag cannot enter twice. */
    CHECK(!cocis_occupancy_enter(&occ, "A1", 1005));
    CHECK_EQ(occ.version, 1);

    CHECK(cocis_occupancy_enter(&occ, "B2", 2000));
    CHECK_EQ(cocis_occupancy_count(&occ), 2);
    CHECK_EQ(occ.version, 2);

    const cocis_occupant_t *first = cocis_occupancy_at(&occ, 0);
    CHECK(first != NULL);
    CHECK_EQ(first->since_utc, 1000);
    CHECK(cocis_occupancy_at(&occ, 2) == NULL);

    CHECK(cocis_occupancy_leave(&occ, "A1"));
    CHECK(!cocis_occupancy_contains(&occ, "A1"));
    CHECK_EQ(cocis_occupancy_count(&occ), 1);
    CHECK_EQ(occ.version, 3);

    /* Leaving twice is an anomaly. */
    CHECK(!cocis_occupancy_leave(&occ, "A1"));

    /* Invalid input never changes the table. */
    CHECK(!cocis_occupancy_enter(&occ, "", 1));
    CHECK(!cocis_occupancy_enter(&occ, NULL, 1));
    CHECK(!cocis_occupancy_leave(&occ, NULL));
}

void test_occupancy_capacity(void)
{
    cocis_occupancy_t occ;
    cocis_occupancy_init(&occ);

    for (int i = 0; i < COCIS_OCCUPANCY_MAX; i++) {
        char uid[16];
        snprintf(uid, sizeof(uid), "U%03d", i);
        CHECK(cocis_occupancy_enter(&occ, uid, (uint32_t)i));
    }
    CHECK_EQ(cocis_occupancy_count(&occ), COCIS_OCCUPANCY_MAX);
    CHECK(!cocis_occupancy_enter(&occ, "EXTRA", 0));

    /* Freeing one slot allows exactly one more person. */
    CHECK(cocis_occupancy_leave(&occ, "U000"));
    CHECK(cocis_occupancy_enter(&occ, "EXTRA", 0));
    CHECK_EQ(cocis_occupancy_count(&occ), COCIS_OCCUPANCY_MAX);
}
