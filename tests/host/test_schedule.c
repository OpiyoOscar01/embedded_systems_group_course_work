/**
 * @file test_schedule.c
 * @brief Host tests for the schedule parser and evaluator.
 * @author Opiyo Oscar
 * @requirements FR-SCH-01, FR-SCH-02, FR-SCH-03, FR-SCH-05, FR-SCH-07
 */

#include "cocis_time.h"
#include "test_util.h"

#include <string.h>

void test_schedule_defaults(void)
{
    cocis_schedule_t s;
    cocis_schedule_default(&s);

    /* Monday 2026-10-26, default 07:00-21:00. */
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 26, 6, 59), COCIS_SCHED_CLOSED_HOURS);
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 26, 7, 0), COCIS_SCHED_OPEN);
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 26, 20, 59), COCIS_SCHED_OPEN);
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 26, 21, 0), COCIS_SCHED_CLOSED_HOURS);

    /* Saturday 2026-10-31 closes at 14:00. */
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 31, 8, 0), COCIS_SCHED_OPEN);
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 31, 13, 59), COCIS_SCHED_OPEN);
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 31, 14, 0), COCIS_SCHED_CLOSED_HOURS);

    /* Sunday 2026-11-01 is closed. */
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 11, 1, 12, 0), COCIS_SCHED_CLOSED_WEEKDAY);

    CHECK(cocis_schedule_is_open(&s, true, 2026, 10, 26, 9, 0));
    CHECK(!cocis_schedule_is_open(&s, true, 2026, 11, 1, 9, 0));
}

void test_schedule_time_validity(void)
{
    cocis_schedule_t s;
    cocis_schedule_default(&s);

    /* No valid clock always fails secure, even inside a window. */
    CHECK_EQ(cocis_schedule_eval(&s, false, 2026, 10, 26, 9, 0), COCIS_SCHED_CLOSED_NO_TIME);
    CHECK_EQ(cocis_schedule_eval(NULL, true, 2026, 10, 26, 9, 0), COCIS_SCHED_CLOSED_NO_TIME);
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 13, 26, 9, 0), COCIS_SCHED_CLOSED_NO_TIME);
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 26, 24, 0), COCIS_SCHED_CLOSED_NO_TIME);
}

void test_schedule_weekday_math(void)
{
    CHECK_EQ(cocis_weekday_from_ymd(2026, 10, 29), COCIS_WEEKDAY_THU);
    CHECK_EQ(cocis_weekday_from_ymd(2026, 10, 26), COCIS_WEEKDAY_MON);
    CHECK_EQ(cocis_weekday_from_ymd(2026, 11, 1), COCIS_WEEKDAY_SUN);
    CHECK_EQ(cocis_weekday_from_ymd(2024, 2, 29), COCIS_WEEKDAY_THU);
    CHECK_EQ(cocis_weekday_from_ymd(2026, 2, 29), -1);
    CHECK_EQ(cocis_weekday_from_ymd(2026, 2, 30), -1);
    CHECK_EQ(cocis_weekday_from_ymd(2026, 0, 1), -1);
}

void test_schedule_parse(void)
{
    uint16_t m = 999;

    CHECK(cocis_time_parse_hhmm("00:00", &m));
    CHECK_EQ(m, 0);
    CHECK(cocis_time_parse_hhmm("07:05", &m));
    CHECK_EQ(m, 425);
    CHECK(cocis_time_parse_hhmm("23:59", &m));
    CHECK_EQ(m, 1439);

    CHECK(!cocis_time_parse_hhmm("7:00", &m));
    CHECK(!cocis_time_parse_hhmm("07:0", &m));
    CHECK(!cocis_time_parse_hhmm("24:00", &m));
    CHECK(!cocis_time_parse_hhmm("07:60", &m));
    CHECK(!cocis_time_parse_hhmm("aa:bb", &m));
    CHECK(!cocis_time_parse_hhmm(NULL, &m));
    CHECK(!cocis_time_parse_hhmm("07:00", NULL));
}

void test_schedule_edit(void)
{
    cocis_schedule_t s;
    cocis_schedule_default(&s);

    CHECK(cocis_schedule_set_day(&s, COCIS_WEEKDAY_TUE, "09:00", "17:00"));
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 27, 8, 59), COCIS_SCHED_CLOSED_HOURS);
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 27, 9, 0), COCIS_SCHED_OPEN);

    /* Midnight crossing and equal endpoints are rejected. */
    CHECK(!cocis_schedule_set_day(&s, COCIS_WEEKDAY_WED, "22:00", "06:00"));
    CHECK(!cocis_schedule_set_day(&s, COCIS_WEEKDAY_WED, "09:00", "09:00"));

    cocis_schedule_close_day(&s, COCIS_WEEKDAY_WED);
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 28, 12, 0), COCIS_SCHED_CLOSED_WEEKDAY);
}

void test_schedule_json(void)
{
    cocis_schedule_t original;
    cocis_schedule_default(&original);
    original.version = 7;
    CHECK(cocis_schedule_set_day(&original, COCIS_WEEKDAY_WED, "09:00", "17:00"));
    CHECK(cocis_schedule_add_holiday(&original, "2026-12-25"));

    char json[512];
    int length = cocis_schedule_to_json(&original, json, sizeof(json));
    CHECK(length > 0);
    CHECK(strstr(json, "\"version\":7") != NULL);
    CHECK(strstr(json, "\"sun\":null") != NULL);
    CHECK(strstr(json, "\"2026-12-25\"") != NULL);

    cocis_schedule_t parsed;
    CHECK(cocis_schedule_from_json(json, &parsed));
    CHECK_EQ(parsed.version, 7);
    CHECK_EQ(parsed.holiday_count, 1);
    CHECK(strcmp(parsed.holidays[0], "2026-12-25") == 0);
    CHECK(parsed.days[COCIS_WEEKDAY_SUN].closed);
    CHECK_EQ(parsed.days[COCIS_WEEKDAY_WED].open_min, 9 * 60);
    CHECK_EQ(parsed.days[COCIS_WEEKDAY_WED].close_min, 17 * 60);
    CHECK_EQ(parsed.days[COCIS_WEEKDAY_MON].open_min, 7 * 60);

    /* Whitespace, missing days and unknown fields are tolerated. */
    const char *text =
        " { \"version\" : 3 , \"days\" : { \"mon\" : [ \"08:00\" , \"12:00\" ] ,"
        " \"tue\" : null } , \"holidays\" : [ ] , \"note\" : {\"x\":1} } ";
    CHECK(cocis_schedule_from_json(text, &parsed));
    CHECK_EQ(parsed.version, 3);
    CHECK_EQ(parsed.days[COCIS_WEEKDAY_MON].open_min, 8 * 60);
    CHECK(parsed.days[COCIS_WEEKDAY_TUE].closed);
    CHECK(parsed.days[COCIS_WEEKDAY_WED].closed);

    /* Malformed or incomplete documents are rejected. */
    CHECK(!cocis_schedule_from_json("{", &parsed));
    CHECK(!cocis_schedule_from_json("{\"days\":{}}", &parsed));
    CHECK(!cocis_schedule_from_json("{\"version\":1}", &parsed));
    CHECK(!cocis_schedule_from_json(
        "{\"version\":1,\"days\":{\"mon\":[\"10:00\",\"09:00\"]}}", &parsed));
    CHECK(!cocis_schedule_from_json(
        "{\"version\":1,\"days\":{},\"holidays\":[\"bad\"]}", &parsed));
    CHECK(!cocis_schedule_from_json(NULL, &parsed));

    /* The writer reports overflow instead of truncating. */
    CHECK_EQ(cocis_schedule_to_json(&original, json, 8), -1);
}

void test_schedule_holidays(void)
{
    cocis_schedule_t s;
    cocis_schedule_default(&s);

    CHECK(cocis_schedule_add_holiday(&s, "2026-10-26"));
    CHECK(cocis_schedule_is_holiday(&s, 2026, 10, 26));
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 26, 12, 0), COCIS_SCHED_CLOSED_HOLIDAY);

    /* Duplicates and invalid dates are rejected. */
    CHECK(!cocis_schedule_add_holiday(&s, "2026-10-26"));
    CHECK(!cocis_schedule_add_holiday(&s, "2026-02-30"));
    CHECK(!cocis_schedule_add_holiday(&s, "26-10-26"));
    CHECK(!cocis_schedule_add_holiday(&s, NULL));

    CHECK(cocis_schedule_del_holiday(&s, "2026-10-26"));
    CHECK(!cocis_schedule_is_holiday(&s, 2026, 10, 26));
    CHECK_EQ(cocis_schedule_eval(&s, true, 2026, 10, 26, 12, 0), COCIS_SCHED_OPEN);
    CHECK(!cocis_schedule_del_holiday(&s, "2026-10-26"));

    /* Capacity is enforced. */
    for (int i = 0; i < COCIS_HOLIDAY_MAX; i++) {
        char date[11];
        snprintf(date, sizeof(date), "2026-11-%02d", i + 1);
        CHECK(cocis_schedule_add_holiday(&s, date));
    }
    CHECK(!cocis_schedule_add_holiday(&s, "2026-12-01"));
}
