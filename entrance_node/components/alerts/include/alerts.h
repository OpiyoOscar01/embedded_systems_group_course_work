/**
 * @file alerts.h
 * @brief Alert counters for repeated denials and tamper rate limiting.
 *
 * Brute-force detection is per tag: the third consecutive denial raises an
 * alert once. A successful scan resets the counter. Tamper alerts are rate
 * limited so a loose wire does not flood the log.
 *
 * @author Opiyo Oscar
 * @requirements FR-ALR-01, FR-ALR-03, FR-ALR-04
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cocis_types.h"

#define COCIS_ALERT_MAX_UID     16
#define COCIS_DENIAL_THRESHOLD  3
#define COCIS_TAMPER_QUIET_MS   10000u

typedef struct {
    char uid[COCIS_UID_MAX];
    uint8_t denials;
} cocis_denial_entry_t;

typedef struct {
    cocis_denial_entry_t entries[COCIS_ALERT_MAX_UID];
    size_t count;
    bool have_tamper;
    uint32_t last_tamper_ms;
} cocis_alerts_t;

/** Empty all counters. */
void cocis_alerts_init(cocis_alerts_t *alerts);

/**
 * Record one denied scan.
 * Returns true exactly when the threshold is reached, so the caller emits one
 * alert rather than one per failed attempt.
 */
bool cocis_alerts_note_denial(cocis_alerts_t *alerts, const char *uid);

/** Reset a tag's denial counter after a successful scan. */
void cocis_alerts_clear_uid(cocis_alerts_t *alerts, const char *uid);

/**
 * Rate-limited tamper gate.
 * Returns true at most once per COCIS_TAMPER_QUIET_MS.
 */
bool cocis_alerts_tamper_allowed(cocis_alerts_t *alerts, uint32_t now_ms);
