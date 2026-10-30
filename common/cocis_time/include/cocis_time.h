/**
 * @file cocis_time.h
 * @brief Pure schedule model and evaluator (East Africa Time).
 * @author Opiyo Oscar
 * @requirements FR-SCH-01, FR-SCH-02, FR-SCH-03, FR-SCH-05, FR-SCH-07
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COCIS_TIME_VERSION        1
#define COCIS_HOLIDAY_MAX         16
#define COCIS_HOLIDAY_DATE_LEN    10
#define COCIS_MINUTES_PER_DAY     1440

/* Weekday index, Monday first to match the schedule JSON keys. */
typedef enum {
    COCIS_WEEKDAY_MON = 0,
    COCIS_WEEKDAY_TUE,
    COCIS_WEEKDAY_WED,
    COCIS_WEEKDAY_THU,
    COCIS_WEEKDAY_FRI,
    COCIS_WEEKDAY_SAT,
    COCIS_WEEKDAY_SUN,
    COCIS_WEEKDAY_COUNT,
} cocis_weekday_t;

/* One weekday window. closed == true means no access that day. */
typedef struct {
    bool closed;
    uint16_t open_min;
    uint16_t close_min;
} cocis_day_t;

/* A versioned weekly schedule with a holiday list. */
typedef struct {
    uint32_t version;
    cocis_day_t days[COCIS_WEEKDAY_COUNT];
    char holidays[COCIS_HOLIDAY_MAX][COCIS_HOLIDAY_DATE_LEN + 1];
    size_t holiday_count;
} cocis_schedule_t;

/* Result of evaluating a timestamp against the schedule. */
typedef enum {
    COCIS_SCHED_OPEN = 0,
    COCIS_SCHED_CLOSED_NO_TIME,
    COCIS_SCHED_CLOSED_HOLIDAY,
    COCIS_SCHED_CLOSED_WEEKDAY,
    COCIS_SCHED_CLOSED_HOURS,
} cocis_schedule_state_t;

/* Mon-Fri 07:00-21:00, Sat 08:00-14:00, Sun closed, no holidays. */
void cocis_schedule_default(cocis_schedule_t *schedule);

/* Parse strict "HH:MM" into minutes since midnight. */
bool cocis_time_parse_hhmm(const char *text, uint16_t *minutes);

/* Set or close a weekday window. Rejects equal or midnight-crossing times. */
bool cocis_schedule_set_day(cocis_schedule_t *schedule, cocis_weekday_t day,
                            const char *open_hhmm, const char *close_hhmm);
void cocis_schedule_close_day(cocis_schedule_t *schedule, cocis_weekday_t day);

/* Add or remove a "YYYY-MM-DD" holiday. Rejects duplicates and bad dates. */
bool cocis_schedule_add_holiday(cocis_schedule_t *schedule, const char *ymd);
bool cocis_schedule_del_holiday(cocis_schedule_t *schedule, const char *ymd);
bool cocis_schedule_is_holiday(const cocis_schedule_t *schedule,
                               int year, int month, int day);

/* 0 = Monday .. 6 = Sunday, -1 on an invalid date. */
int cocis_weekday_from_ymd(int year, int month, int day);

/*
 * Decide whether a scan at the given local time is inside opening hours.
 * Invalid/unsynchronised time always fails secure (CLOSED_NO_TIME).
 */
cocis_schedule_state_t cocis_schedule_eval(const cocis_schedule_t *schedule,
                                           bool time_valid,
                                           int year, int month, int day,
                                           int hour, int minute);

/* Convenience wrapper: true only for COCIS_SCHED_OPEN. */
bool cocis_schedule_is_open(const cocis_schedule_t *schedule, bool time_valid,
                            int year, int month, int day, int hour, int minute);

/*
 * JSON (de)serialisation for the schedule, used for NVS `sched/json_blob`
 * and /sdcard/config/schedule.json.
 *
 * Writer: returns the number of bytes written (excluding NUL) or -1.
 * Parser: accepts unknown fields, requires version and days.
 */
int cocis_schedule_to_json(const cocis_schedule_t *schedule, char *buf, size_t buf_len);
bool cocis_schedule_from_json(const char *json, cocis_schedule_t *schedule);

/* Human readable state for logs and the console. */
static inline const char *cocis_schedule_state_str(cocis_schedule_state_t state)
{
    switch (state) {
    case COCIS_SCHED_OPEN:              return "OPEN";
    case COCIS_SCHED_CLOSED_NO_TIME:    return "CLOSED_NO_TIME";
    case COCIS_SCHED_CLOSED_HOLIDAY:    return "CLOSED_HOLIDAY";
    case COCIS_SCHED_CLOSED_WEEKDAY:    return "CLOSED_WEEKDAY";
    case COCIS_SCHED_CLOSED_HOURS:      return "CLOSED_HOURS";
    default:                            return "UNKNOWN";
    }
}
