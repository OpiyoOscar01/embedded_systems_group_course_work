/**
 * @file scan_filter.h
 * @brief Duplicate-scan debounce for the RFID reader.
 *
 * Readers often report the same tag several times while it is held near the
 * antenna. The same UID within COCIS_DUP_SCAN_MS is treated as one scan. A
 * different UID is always accepted immediately, so a queue of people is not
 * slowed down.
 *
 * @author Opiyo Oscar
 * @requirements FR-ACC-06
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cocis_types.h"

typedef struct {
    char last_uid[COCIS_UID_MAX];
    uint32_t last_ms;
    bool has_last;
} cocis_scan_filter_t;

/** Clear the filter (no previous scan). */
void cocis_scan_filter_init(cocis_scan_filter_t *filter);

/**
 * Decide whether a scan should be processed.
 * Returns false for a repeat of the same UID inside the debounce window.
 */
bool cocis_scan_filter_accept(cocis_scan_filter_t *filter, const char *uid, uint32_t now_ms);
