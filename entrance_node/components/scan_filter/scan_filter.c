/**
 * @file scan_filter.c
 * @brief Duplicate-scan debounce (pure logic).
 * @author Opiyo Oscar
 * @requirements FR-ACC-06
 */

#include "scan_filter.h"

#include <string.h>

void cocis_scan_filter_init(cocis_scan_filter_t *filter)
{
    if (filter == NULL) {
        return;
    }
    memset(filter, 0, sizeof(*filter));
}

bool cocis_scan_filter_accept(cocis_scan_filter_t *filter, const char *uid, uint32_t now_ms)
{
    if (filter == NULL || uid == NULL || uid[0] == '\0') {
        return false;
    }

    if (filter->has_last && strcmp(filter->last_uid, uid) == 0) {
        uint32_t elapsed = now_ms - filter->last_ms;
        if (elapsed < COCIS_DUP_SCAN_MS) {
            return false;   /* same tag still being presented */
        }
    }

    strncpy(filter->last_uid, uid, sizeof(filter->last_uid) - 1);
    filter->last_uid[sizeof(filter->last_uid) - 1] = '\0';
    filter->last_ms = now_ms;
    filter->has_last = true;
    return true;
}
