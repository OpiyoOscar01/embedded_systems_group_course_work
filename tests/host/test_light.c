/**
 * @file test_light.c
 * @brief Host tests for the lighting FSM, daylight hysteresis and energy maths.
 * @author Opiyo Oscar
 * @requirements FR-LGT-01..07
 */

#include "daylight.h"
#include "energy.h"
#include "light_fsm.h"
#include "test_util.h"

/** Short deterministic timings for the tests (seconds). */
static cocis_light_config_t test_config(void)
{
    cocis_light_config_t cfg = {
        .t_vac_s = 10,
        .t_warn_s = 4,
        .override_s = 60,
    };
    return cfg;
}

void test_light_occupancy_and_warning(void)
{
    cocis_light_fsm_t fsm;
    cocis_light_config_t cfg = test_config();
    cocis_light_init(&fsm, &cfg);
    CHECK_EQ(fsm.state, COCIS_LIGHT_OFF);

    /* Motion in the dark turns the lamp on. */
    CHECK(cocis_light_update(&fsm, 0, true, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_ON);

    /* Still occupied before the vacancy timeout. */
    CHECK(cocis_light_update(&fsm, 9000, false, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_ON);

    /* Vacancy timeout starts the warning; the first blink phase is on. */
    CHECK(cocis_light_update(&fsm, 10000, false, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_WARNING);
    CHECK(cocis_light_is_warning(&fsm));
    CHECK(fsm.relay_on);

    /* 1 Hz blink: off after 500 ms, on again after 1000 ms. */
    CHECK(!cocis_light_update(&fsm, 10500, false, false));
    CHECK(cocis_light_update(&fsm, 11000, false, false));

    /* Warning expires and the lamp goes off. */
    CHECK(!cocis_light_update(&fsm, 14000, false, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_OFF);

    /* Motion during the warning cancels it immediately. */
    cocis_light_init(&fsm, &cfg);
    CHECK(cocis_light_update(&fsm, 0, true, false));
    CHECK(cocis_light_update(&fsm, 10000, false, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_WARNING);
    CHECK(cocis_light_update(&fsm, 10100, true, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_ON);
    CHECK(cocis_light_update(&fsm, 11000, false, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_ON);
}

void test_light_daylight(void)
{
    cocis_light_fsm_t fsm;
    cocis_light_config_t cfg = test_config();
    cocis_light_init(&fsm, &cfg);

    CHECK(cocis_light_update(&fsm, 0, true, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_ON);

    /* Daylight switches the lamp off even while the room is occupied. */
    CHECK(!cocis_light_update(&fsm, 1000, false, true));
    CHECK_EQ(fsm.state, COCIS_LIGHT_OFF);

    /* Motion while still bright keeps it off. */
    CHECK(!cocis_light_update(&fsm, 2000, true, true));

    /* When it gets dark again, occupancy turns it back on. */
    CHECK(cocis_light_update(&fsm, 3000, false, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_ON);
}

void test_light_override_control(void)
{
    cocis_light_fsm_t fsm;
    cocis_light_config_t cfg = test_config();
    cocis_light_init(&fsm, &cfg);

    cocis_light_override(&fsm, 0, 5000);
    CHECK_EQ(fsm.state, COCIS_LIGHT_ON);
    CHECK(cocis_light_update(&fsm, 1000, false, false));

    /* Auto cancels the forced-on state; dark and vacant -> off. */
    cocis_light_cancel_override(&fsm);
    CHECK(!cocis_light_update(&fsm, 2000, false, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_OFF);

    /* Forced off wins over a pending override and occupancy. */
    CHECK(cocis_light_update(&fsm, 3000, true, false));
    cocis_light_override(&fsm, 3500, 5000);
    cocis_light_force_off(&fsm);
    CHECK_EQ(fsm.state, COCIS_LIGHT_OFF);
    CHECK(!cocis_light_update(&fsm, 4000, false, false));

    /* Motion after a forced off starts a new automatic cycle. */
    CHECK(cocis_light_update(&fsm, 5000, true, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_ON);
}

void test_light_override(void)
{
    cocis_light_fsm_t fsm;
    cocis_light_config_t cfg = test_config();
    cocis_light_init(&fsm, &cfg);

    cocis_light_override(&fsm, 0, 5000);
    CHECK_EQ(cocis_light_override_remaining_ms(&fsm, 1000), 4000);
    CHECK(cocis_light_update(&fsm, 1000, false, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_ON);

    /* Override wins over daylight. */
    CHECK(cocis_light_update(&fsm, 2000, false, true));

    /* Expiry returns control to the automatic rules. */
    CHECK(!cocis_light_update(&fsm, 6000, false, false));
    CHECK_EQ(fsm.state, COCIS_LIGHT_OFF);
    CHECK_EQ(cocis_light_override_remaining_ms(&fsm, 6000), 0);

    /* A zero duration uses the configured default (60 s). */
    cocis_light_override(&fsm, 0, 0);
    CHECK_EQ(cocis_light_override_remaining_ms(&fsm, 0), 60000);
    CHECK(cocis_light_update(&fsm, 59999, false, false));
    CHECK(!cocis_light_update(&fsm, 60000, false, false));

    /* An uninitialised FSM must not drive the relay. */
    cocis_light_fsm_t blank = {0};
    CHECK(!cocis_light_update(&blank, 0, true, false));
    CHECK(!cocis_light_update(NULL, 0, true, false));
}

void test_daylight_hysteresis(void)
{
    CHECK(cocis_daylight_update(false, 900));
    CHECK(cocis_daylight_update(true, 700));
    CHECK(!cocis_daylight_update(true, 500));
    CHECK(!cocis_daylight_update(false, 700));

    /* Exact thresholds: 600 is dark, 800 is bright. */
    CHECK(!cocis_daylight_update(true, 600));
    CHECK(cocis_daylight_update(false, 800));
}

void test_energy_accounting(void)
{
    cocis_energy_t energy;
    cocis_energy_init(&energy);
    CHECK_EQ(energy.lamp_on_s, 0);

    /* One hour with the lamp on: 72 W * 1 h = 72 Wh = 72000 mWh. */
    cocis_energy_add(&energy, 3600, true, true);
    CHECK_EQ(energy.lamp_on_s, 3600);
    CHECK_EQ(energy.baseline_s, 3600);
    CHECK_EQ(cocis_energy_mwh_used(&energy, 72), 72000);
    CHECK_EQ(cocis_energy_mwh_saved(&energy, 72), 0);

    /* Half an hour saved versus the baseline. */
    cocis_energy_add(&energy, 1800, false, true);
    CHECK_EQ(energy.lamp_on_s, 3600);
    CHECK_EQ(energy.baseline_s, 5400);
    CHECK_EQ(cocis_energy_mwh_used(&energy, 72), 72000);
    CHECK_EQ(cocis_energy_mwh_saved(&energy, 72), 36000);

    /* Never report negative savings when the lamp ran longer than baseline. */
    cocis_energy_add(&energy, 3600, true, false);
    CHECK_EQ(energy.lamp_on_s, 7200);
    CHECK_EQ(energy.baseline_s, 5400);
    CHECK_EQ(cocis_energy_mwh_saved(&energy, 72), 0);

    CHECK_EQ(cocis_energy_mwh_used(NULL, 72), 0);
    CHECK_EQ(cocis_energy_mwh_saved(NULL, 72), 0);
}
