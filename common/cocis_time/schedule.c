/**
 * @file schedule.c
 * @brief Pure weekly schedule parsing and evaluation.
 * @author Opiyo Oscar
 * @requirements FR-SCH-01, FR-SCH-02, FR-SCH-03, FR-SCH-05, FR-SCH-07
 */

#include "cocis_time.h"

#include <stdio.h>
#include <string.h>

static bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

static bool parse_ymd(const char *text, int *year, int *month, int *day)
{
    if (text == NULL || strlen(text) != COCIS_HOLIDAY_DATE_LEN) {
        return false;
    }
    if (text[4] != '-' || text[7] != '-') {
        return false;
    }

    int y = 0;
    int m = 0;
    int d = 0;
    for (int i = 0; i < 4; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        y = y * 10 + (text[i] - '0');
    }
    for (int i = 5; i < 7; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        m = m * 10 + (text[i] - '0');
    }
    for (int i = 8; i < 10; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        d = d * 10 + (text[i] - '0');
    }

    if (cocis_weekday_from_ymd(y, m, d) < 0) {
        return false;
    }

    *year = y;
    *month = m;
    *day = d;
    return true;
}

void cocis_schedule_default(cocis_schedule_t *schedule)
{
    if (schedule == NULL) {
        return;
    }

    memset(schedule, 0, sizeof(*schedule));
    schedule->version = 1;

    for (int day = COCIS_WEEKDAY_MON; day <= COCIS_WEEKDAY_FRI; day++) {
        schedule->days[day].closed = false;
        schedule->days[day].open_min = 7 * 60;
        schedule->days[day].close_min = 21 * 60;
    }

    schedule->days[COCIS_WEEKDAY_SAT].closed = false;
    schedule->days[COCIS_WEEKDAY_SAT].open_min = 8 * 60;
    schedule->days[COCIS_WEEKDAY_SAT].close_min = 14 * 60;

    schedule->days[COCIS_WEEKDAY_SUN].closed = true;
    schedule->holiday_count = 0;
}

bool cocis_time_parse_hhmm(const char *text, uint16_t *minutes)
{
    if (text == NULL || minutes == NULL) {
        return false;
    }
    if (strlen(text) != 5 || text[2] != ':') {
        return false;
    }
    for (int i = 0; i < 5; i++) {
        if (i == 2) {
            continue;
        }
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }

    int hour = (text[0] - '0') * 10 + (text[1] - '0');
    int minute = (text[3] - '0') * 10 + (text[4] - '0');
    if (hour > 23 || minute > 59) {
        return false;
    }

    *minutes = (uint16_t)(hour * 60 + minute);
    return true;
}

bool cocis_schedule_set_day(cocis_schedule_t *schedule, cocis_weekday_t day,
                            const char *open_hhmm, const char *close_hhmm)
{
    if (schedule == NULL || day < 0 || day >= COCIS_WEEKDAY_COUNT) {
        return false;
    }

    uint16_t open_min = 0;
    uint16_t close_min = 0;
    if (!cocis_time_parse_hhmm(open_hhmm, &open_min) ||
        !cocis_time_parse_hhmm(close_hhmm, &close_min)) {
        return false;
    }

    /* Midnight-crossing windows are not supported (spec 16.1 rule 2). */
    if (open_min >= close_min) {
        return false;
    }

    schedule->days[day].closed = false;
    schedule->days[day].open_min = open_min;
    schedule->days[day].close_min = close_min;
    return true;
}

void cocis_schedule_close_day(cocis_schedule_t *schedule, cocis_weekday_t day)
{
    if (schedule == NULL || day < 0 || day >= COCIS_WEEKDAY_COUNT) {
        return;
    }
    schedule->days[day].closed = true;
    schedule->days[day].open_min = 0;
    schedule->days[day].close_min = 0;
}

int cocis_weekday_from_ymd(int year, int month, int day)
{
    static const int month_offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    if (year < 1970 || month < 1 || month > 12) {
        return -1;
    }
    if (day < 1 || day > days_in_month(year, month)) {
        return -1;
    }

    /* Sakamoto: 0 = Sunday .. 6 = Saturday. */
    int y = year;
    if (month < 3) {
        y -= 1;
    }
    int sunday_based = (y + y / 4 - y / 100 + y / 400 +
                        month_offsets[month - 1] + day) % 7;

    return (sunday_based + 6) % 7;
}

bool cocis_schedule_is_holiday(const cocis_schedule_t *schedule,
                               int year, int month, int day)
{
    if (schedule == NULL) {
        return false;
    }

    char key[COCIS_HOLIDAY_DATE_LEN + 1];
    int written = snprintf(key, sizeof(key), "%04d-%02d-%02d", year, month, day);
    if (written != COCIS_HOLIDAY_DATE_LEN) {
        return false;
    }

    for (size_t i = 0; i < schedule->holiday_count; i++) {
        if (strcmp(schedule->holidays[i], key) == 0) {
            return true;
        }
    }
    return false;
}

bool cocis_schedule_add_holiday(cocis_schedule_t *schedule, const char *ymd)
{
    if (schedule == NULL) {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (!parse_ymd(ymd, &year, &month, &day)) {
        return false;
    }
    if (cocis_schedule_is_holiday(schedule, year, month, day)) {
        return false;
    }
    if (schedule->holiday_count >= COCIS_HOLIDAY_MAX) {
        return false;
    }

    memcpy(schedule->holidays[schedule->holiday_count], ymd, COCIS_HOLIDAY_DATE_LEN);
    schedule->holidays[schedule->holiday_count][COCIS_HOLIDAY_DATE_LEN] = '\0';
    schedule->holiday_count++;
    return true;
}

bool cocis_schedule_del_holiday(cocis_schedule_t *schedule, const char *ymd)
{
    if (schedule == NULL || ymd == NULL) {
        return false;
    }

    for (size_t i = 0; i < schedule->holiday_count; i++) {
        if (strcmp(schedule->holidays[i], ymd) == 0) {
            size_t last = schedule->holiday_count - 1;
            if (i != last) {
                memcpy(schedule->holidays[i], schedule->holidays[last],
                       sizeof(schedule->holidays[0]));
            }
            schedule->holiday_count--;
            return true;
        }
    }
    return false;
}

cocis_schedule_state_t cocis_schedule_eval(const cocis_schedule_t *schedule,
                                           bool time_valid,
                                           int year, int month, int day,
                                           int hour, int minute)
{
    if (!time_valid || schedule == NULL) {
        return COCIS_SCHED_CLOSED_NO_TIME;
    }

    int weekday = cocis_weekday_from_ymd(year, month, day);
    if (weekday < 0 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return COCIS_SCHED_CLOSED_NO_TIME;
    }

    if (cocis_schedule_is_holiday(schedule, year, month, day)) {
        return COCIS_SCHED_CLOSED_HOLIDAY;
    }

    const cocis_day_t *window = &schedule->days[weekday];
    if (window->closed || window->open_min >= window->close_min) {
        return COCIS_SCHED_CLOSED_WEEKDAY;
    }

    int now = hour * 60 + minute;
    if (now >= window->open_min && now < window->close_min) {
        return COCIS_SCHED_OPEN;
    }
    return COCIS_SCHED_CLOSED_HOURS;
}

bool cocis_schedule_is_open(const cocis_schedule_t *schedule, bool time_valid,
                            int year, int month, int day, int hour, int minute)
{
    return cocis_schedule_eval(schedule, time_valid, year, month, day, hour, minute) ==
           COCIS_SCHED_OPEN;
}
