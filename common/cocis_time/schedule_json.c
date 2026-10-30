/**
 * @file schedule_json.c
 * @brief Compact JSON reader/writer for the weekly schedule.
 *
 * The format is the one in section 12.3 of the specification:
 *   {"version":12,"days":{"mon":["07:00","21:00"],...,"sun":null},
 *    "holidays":["2026-12-25"]}
 *
 * The parser is deliberately small and only understands the fields this
 * project writes. Unknown fields are skipped, so newer config files remain
 * readable by older firmware.
 *
 * @author Opiyo Oscar
 * @requirements FR-SCH-01, FR-SCH-05, FR-SCH-06
 */

#include "cocis_time.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *DAY_KEYS[COCIS_WEEKDAY_COUNT] = {
    "mon", "tue", "wed", "thu", "fri", "sat", "sun",
};

/* ------------------------------------------------------------------------- */
/* Small parser helpers                                                       */
/* ------------------------------------------------------------------------- */

/** Advance past JSON whitespace. */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

/** Parse a JSON string into a bounded buffer (basic escapes only). */
static bool parse_string(const char **cursor, char *out, size_t out_len)
{
    const char *p = *cursor;
    if (*p != '"') {
        return false;
    }
    p++;

    size_t used = 0;
    while (*p != '"') {
        if (*p == '\0') {
            return false;
        }
        char c = *p;
        if (c == '\\') {
            p++;
            c = *p;
            if (c == '\0') {
                return false;
            }
        }
        if (used + 1 >= out_len) {
            return false;
        }
        out[used++] = c;
        p++;
    }

    out[used] = '\0';
    *cursor = p + 1;
    return true;
}

/** Parse an unsigned integer. */
static bool parse_uint(const char **cursor, uint32_t *out)
{
    const char *p = *cursor;
    if (*p < '0' || *p > '9') {
        return false;
    }

    uint32_t value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10u + (uint32_t)(*p - '0');
        p++;
    }
    *cursor = p;
    *out = value;
    return true;
}

/** Skip one JSON value (string, number, object, array, literal). */
static bool skip_value(const char **cursor)
{
    const char *p = skip_ws(*cursor);

    if (*p == '"') {
        char ignored[8];
        if (!parse_string(&p, ignored, sizeof(ignored))) {
            /* Long strings simply need skipping, not decoding. */
            p++;
            while (*p != '"' && *p != '\0') {
                if (*p == '\\' && p[1] != '\0') {
                    p++;
                }
                p++;
            }
            if (*p != '"') {
                return false;
            }
            p++;
        }
        *cursor = p;
        return true;
    }

    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        int depth = 0;
        bool in_string = false;
        bool escaped = false;

        for (; *p != '\0'; p++) {
            char c = *p;
            if (in_string) {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    in_string = false;
                }
                continue;
            }
            if (c == '"') {
                in_string = true;
            } else if (c == open) {
                depth++;
            } else if (c == close) {
                depth--;
                if (depth == 0) {
                    p++;
                    break;
                }
            }
        }
        if (depth != 0) {
            return false;
        }
        *cursor = p;
        return true;
    }

    /* true / false / null / number */
    const char *start = p;
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }
    *cursor = p;
    return p != start;
}

/** Map a day key to its index, or -1. */
static int day_from_key(const char *key)
{
    for (int i = 0; i < COCIS_WEEKDAY_COUNT; i++) {
        if (strcmp(key, DAY_KEYS[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Parser                                                                     */
/* ------------------------------------------------------------------------- */

/** Parse the "days" object. */
static bool parse_days(const char **cursor, cocis_schedule_t *schedule)
{
    const char *p = skip_ws(*cursor);
    if (*p != '{') {
        return false;
    }
    p++;

    /* Start with every day closed; each present key overrides it. */
    for (int day = 0; day < COCIS_WEEKDAY_COUNT; day++) {
        cocis_schedule_close_day(schedule, (cocis_weekday_t)day);
    }

    while (true) {
        p = skip_ws(p);
        if (*p == '}') {
            *cursor = p + 1;
            return true;
        }

        char key[8];
        if (!parse_string(&p, key, sizeof(key))) {
            return false;
        }
        p = skip_ws(p);
        if (*p != ':') {
            return false;
        }
        p = skip_ws(p + 1);

        int day = day_from_key(key);
        if (day < 0) {
            if (!skip_value(&p)) {
                return false;
            }
        } else if (strncmp(p, "null", 4) == 0) {
            cocis_schedule_close_day(schedule, (cocis_weekday_t)day);
            p += 4;
        } else if (*p == '[') {
            p = skip_ws(p + 1);

            char open_text[6];
            char close_text[6];
            if (!parse_string(&p, open_text, sizeof(open_text))) {
                return false;
            }
            p = skip_ws(p);
            if (*p != ',') {
                return false;
            }
            p = skip_ws(p + 1);
            if (!parse_string(&p, close_text, sizeof(close_text))) {
                return false;
            }
            p = skip_ws(p);
            if (*p != ']') {
                return false;
            }
            p++;

            if (!cocis_schedule_set_day(schedule, (cocis_weekday_t)day,
                                        open_text, close_text)) {
                return false;
            }
        } else {
            return false;
        }

        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            *cursor = p + 1;
            return true;
        }
        return false;
    }
}

/** Parse the "holidays" array of "YYYY-MM-DD" strings. */
static bool parse_holidays(const char **cursor, cocis_schedule_t *schedule)
{
    const char *p = skip_ws(*cursor);
    if (*p != '[') {
        return false;
    }
    p++;

    while (true) {
        p = skip_ws(p);
        if (*p == ']') {
            *cursor = p + 1;
            return true;
        }

        char date[COCIS_HOLIDAY_DATE_LEN + 1];
        if (!parse_string(&p, date, sizeof(date))) {
            return false;
        }
        if (!cocis_schedule_add_holiday(schedule, date)) {
            return false;
        }

        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == ']') {
            *cursor = p + 1;
            return true;
        }
        return false;
    }
}

bool cocis_schedule_from_json(const char *json, cocis_schedule_t *schedule)
{
    if (json == NULL || schedule == NULL) {
        return false;
    }

    cocis_schedule_t parsed;
    cocis_schedule_default(&parsed);
    parsed.version = 0;
    parsed.holiday_count = 0;

    bool have_version = false;
    bool have_days = false;

    const char *p = skip_ws(json);
    if (*p != '{') {
        return false;
    }
    p++;

    while (true) {
        p = skip_ws(p);
        if (*p == '}') {
            p++;
            break;
        }

        char key[24];
        if (!parse_string(&p, key, sizeof(key))) {
            return false;
        }
        p = skip_ws(p);
        if (*p != ':') {
            return false;
        }
        p = skip_ws(p + 1);

        if (strcmp(key, "version") == 0) {
            if (!parse_uint(&p, &parsed.version)) {
                return false;
            }
            have_version = true;
        } else if (strcmp(key, "days") == 0) {
            if (!parse_days(&p, &parsed)) {
                return false;
            }
            have_days = true;
        } else if (strcmp(key, "holidays") == 0) {
            if (!parse_holidays(&p, &parsed)) {
                return false;
            }
        } else if (!skip_value(&p)) {
            return false;
        }

        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }
        return false;
    }

    if (!have_version || !have_days) {
        return false;
    }

    *schedule = parsed;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Writer                                                                     */
/* ------------------------------------------------------------------------- */

/** Append formatted text to a bounded buffer. */
static bool append_text(char *buf, size_t len, size_t *pos, const char *fmt, ...)
{
    if (*pos >= len) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf + *pos, len - *pos, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= len - *pos) {
        return false;
    }
    *pos += (size_t)written;
    return true;
}

/** Format minutes since midnight as "HH:MM". */
static void format_hhmm(uint16_t minutes, char out[6])
{
    unsigned hours = (unsigned)(minutes / 60u);
    unsigned mins = (unsigned)(minutes % 60u);
    if (hours > 23u) {
        hours = 23u;
    }
    if (mins > 59u) {
        mins = 59u;
    }
    snprintf(out, 6, "%02u:%02u", hours, mins);
}

int cocis_schedule_to_json(const cocis_schedule_t *schedule, char *buf, size_t buf_len)
{
    if (schedule == NULL || buf == NULL || buf_len == 0) {
        return -1;
    }

    size_t pos = 0;
    if (!append_text(buf, buf_len, &pos, "{\"version\":%lu,\"days\":{",
                     (unsigned long)schedule->version)) {
        return -1;
    }

    for (int day = 0; day < COCIS_WEEKDAY_COUNT; day++) {
        if (day > 0 && !append_text(buf, buf_len, &pos, ",")) {
            return -1;
        }
        if (!append_text(buf, buf_len, &pos, "\"%s\":", DAY_KEYS[day])) {
            return -1;
        }

        const cocis_day_t *window = &schedule->days[day];
        if (window->closed) {
            if (!append_text(buf, buf_len, &pos, "null")) {
                return -1;
            }
            continue;
        }

        char open_text[6];
        char close_text[6];
        format_hhmm(window->open_min, open_text);
        format_hhmm(window->close_min, close_text);
        if (!append_text(buf, buf_len, &pos, "[\"%s\",\"%s\"]", open_text, close_text)) {
            return -1;
        }
    }

    if (!append_text(buf, buf_len, &pos, "},\"holidays\":[")) {
        return -1;
    }
    for (size_t i = 0; i < schedule->holiday_count; i++) {
        if (i > 0 && !append_text(buf, buf_len, &pos, ",")) {
            return -1;
        }
        if (!append_text(buf, buf_len, &pos, "\"%s\"", schedule->holidays[i])) {
            return -1;
        }
    }
    if (!append_text(buf, buf_len, &pos, "]}")) {
        return -1;
    }

    return (int)pos;
}
