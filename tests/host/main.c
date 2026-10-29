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

    if (g_failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }

    printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
