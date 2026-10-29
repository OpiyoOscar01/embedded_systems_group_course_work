/**
 * @file alerts.c
 * @brief Alert counters and rate limiting (pure logic).
 * @author Opiyo Oscar
 * @requirements FR-ALR-01, FR-ALR-03, FR-ALR-04
 */

#include "alerts.h"

#include <string.h>

void cocis_alerts_init(cocis_alerts_t *alerts)
{
    if (alerts == NULL) {
        return;
    }
    memset(alerts, 0, sizeof(*alerts));
}

/** Find or create the entry for a UID. */
static cocis_denial_entry_t *entry_for(cocis_alerts_t *alerts, const char *uid)
{
    for (size_t i = 0; i < alerts->count; i++) {
        if (strcmp(alerts->entries[i].uid, uid) == 0) {
            return &alerts->entries[i];
        }
    }

    if (alerts->count >= COCIS_ALERT_MAX_UID) {
        return NULL;
    }

    cocis_denial_entry_t *entry = &alerts->entries[alerts->count];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->uid, uid, sizeof(entry->uid) - 1);
    alerts->count++;
    return entry;
}

bool cocis_alerts_note_denial(cocis_alerts_t *alerts, const char *uid)
{
    if (alerts == NULL || uid == NULL || uid[0] == '\0') {
        return false;
    }

    cocis_denial_entry_t *entry = entry_for(alerts, uid);
    if (entry == NULL) {
        return false;
    }

    if (entry->denials < 255) {
        entry->denials++;
    }
    return entry->denials == COCIS_DENIAL_THRESHOLD;
}

void cocis_alerts_clear_uid(cocis_alerts_t *alerts, const char *uid)
{
    if (alerts == NULL || uid == NULL) {
        return;
    }

    for (size_t i = 0; i < alerts->count; i++) {
        if (strcmp(alerts->entries[i].uid, uid) == 0) {
            alerts->entries[i].denials = 0;
            return;
        }
    }
}

bool cocis_alerts_tamper_allowed(cocis_alerts_t *alerts, uint32_t now_ms)
{
    if (alerts == NULL) {
        return false;
    }
    if (alerts->have_tamper &&
        (now_ms - alerts->last_tamper_ms) < COCIS_TAMPER_QUIET_MS) {
        return false;
    }

    alerts->have_tamper = true;
    alerts->last_tamper_ms = now_ms;
    return true;
}
