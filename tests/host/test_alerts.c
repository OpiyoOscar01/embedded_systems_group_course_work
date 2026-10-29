/**
 * @file test_alerts.c
 * @brief Host tests for denial counting and tamper rate limiting.
 * @author Opiyo Oscar
 * @requirements FR-ALR-01, FR-ALR-03, FR-ALR-04
 */

#include "alerts.h"
#include "test_util.h"

void test_alerts_denials(void)
{
    cocis_alerts_t alerts;
    cocis_alerts_init(&alerts);

    CHECK(!cocis_alerts_note_denial(&alerts, "A1"));
    CHECK(!cocis_alerts_note_denial(&alerts, "A1"));

    /* Exactly the third consecutive denial raises the alert. */
    CHECK(cocis_alerts_note_denial(&alerts, "A1"));
    CHECK(!cocis_alerts_note_denial(&alerts, "A1"));

    /* A successful scan resets the counter. */
    cocis_alerts_clear_uid(&alerts, "A1");
    CHECK(!cocis_alerts_note_denial(&alerts, "A1"));
    CHECK(!cocis_alerts_note_denial(&alerts, "A1"));
    CHECK(cocis_alerts_note_denial(&alerts, "A1"));

    /* Different tags are tracked independently. */
    CHECK(!cocis_alerts_note_denial(&alerts, "B2"));

    CHECK(!cocis_alerts_note_denial(&alerts, NULL));
    CHECK(!cocis_alerts_note_denial(&alerts, ""));
    CHECK(!cocis_alerts_note_denial(NULL, "A1"));
}

void test_alerts_capacity(void)
{
    cocis_alerts_t alerts;
    cocis_alerts_init(&alerts);

    for (int i = 0; i < COCIS_ALERT_MAX_UID; i++) {
        char uid[16];
        snprintf(uid, sizeof(uid), "U%03d", i);
        CHECK(!cocis_alerts_note_denial(&alerts, uid));
    }

    /* Beyond capacity the counter is ignored rather than crashing. */
    CHECK(!cocis_alerts_note_denial(&alerts, "EXTRA"));
    CHECK(!cocis_alerts_note_denial(&alerts, "EXTRA"));
    CHECK(!cocis_alerts_note_denial(&alerts, "EXTRA"));
}

void test_alerts_tamper(void)
{
    cocis_alerts_t alerts;
    cocis_alerts_init(&alerts);

    CHECK(cocis_alerts_tamper_allowed(&alerts, 0));
    CHECK(!cocis_alerts_tamper_allowed(&alerts, 9999));
    CHECK(cocis_alerts_tamper_allowed(&alerts, 10000));
    CHECK(!cocis_alerts_tamper_allowed(&alerts, 15000));
    CHECK(cocis_alerts_tamper_allowed(&alerts, 20000));

    CHECK(!cocis_alerts_tamper_allowed(NULL, 0));
}
