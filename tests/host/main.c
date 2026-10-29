/**
 * @file main.c
 * @brief Host test runner.
 * @author Opiyo Oscar
 * @requirements NFR-07
 */

#include <stdio.h>

#include "test_util.h"

int g_failures = 0;

void test_types(void);
void test_log_chain(void);
void test_log_chain_limits(void);
void test_schedule_defaults(void);
void test_schedule_time_validity(void);
void test_schedule_weekday_math(void);
void test_schedule_parse(void);
void test_schedule_edit(void);
void test_schedule_holidays(void);
void test_proto_event_ids(void);
void test_proto_encode_decode(void);
void test_proto_escaping(void);
void test_proto_decode_edge_cases(void);
void test_proto_topics(void);
void test_proto_dedupe(void);
void test_registry_basic(void);
void test_registry_capacity(void);
void test_registry_expiry(void);
void test_registry_csv(void);
void test_access_identity(void);
void test_access_emergency(void);
void test_access_schedule(void);
void test_access_antipassback(void);
void test_occupancy_flow(void);
void test_occupancy_capacity(void);
void test_door_timing(void);
void test_door_defaults_and_wrap(void);
void test_light_occupancy_and_warning(void);
void test_light_daylight(void);
void test_light_override(void);
void test_daylight_hysteresis(void);
void test_energy_accounting(void);
void test_auth_login(void);
void test_auth_lockout(void);
void test_auth_idle_and_roles(void);
void test_auth_must_change(void);
void test_auth_limits(void);
void test_scan_debounce(void);
void test_scan_debounce_wrap(void);
void test_emergency_state(void);
void test_emergency_roll_call(void);
void test_alerts_denials(void);
void test_alerts_capacity(void);
void test_alerts_tamper(void);

int main(void)
{
    printf("Running host tests...\n");

    test_types();
    test_log_chain();
    test_log_chain_limits();
    test_schedule_defaults();
    test_schedule_time_validity();
    test_schedule_weekday_math();
    test_schedule_parse();
    test_schedule_edit();
    test_schedule_holidays();
    test_proto_event_ids();
    test_proto_encode_decode();
    test_proto_escaping();
    test_proto_decode_edge_cases();
    test_proto_topics();
    test_proto_dedupe();
    test_registry_basic();
    test_registry_capacity();
    test_registry_expiry();
    test_registry_csv();
    test_access_identity();
    test_access_emergency();
    test_access_schedule();
    test_access_antipassback();
    test_occupancy_flow();
    test_occupancy_capacity();
    test_door_timing();
    test_door_defaults_and_wrap();
    test_light_occupancy_and_warning();
    test_light_daylight();
    test_light_override();
    test_daylight_hysteresis();
    test_energy_accounting();
    test_auth_login();
    test_auth_lockout();
    test_auth_idle_and_roles();
    test_auth_must_change();
    test_auth_limits();
    test_scan_debounce();
    test_scan_debounce_wrap();
    test_emergency_state();
    test_emergency_roll_call();
    test_alerts_denials();
    test_alerts_capacity();
    test_alerts_tamper();

    if (g_failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }

    printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
