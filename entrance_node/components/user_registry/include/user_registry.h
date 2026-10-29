/**
 * @file user_registry.h
 * @brief In-memory tag/user registry with CSV (de)serialisation.
 *
 * The registry is the source of truth for access decisions. It is kept in RAM
 * for fast lookups during a scan and persisted as /sdcard/config/users.csv.
 * Only hashes of credentials live in NVS; this table stores public metadata.
 *
 * @author Opiyo Oscar
 * @requirements FR-ACC-08, FR-CLI-04
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cocis_types.h"

#define COCIS_REGISTRY_MAX  32
#define COCIS_EXPIRY_LEN    10   /* "YYYY-MM-DD" */
#define COCIS_CREATED_LEN   20   /* "YYYY-MM-DDTHH:MM:SS" */

/* One registered tag holder. */
typedef struct {
    char uid[COCIS_UID_MAX];
    char name[COCIS_NAME_MAX];
    cocis_role_t role;
    cocis_user_status_t status;
    char expiry[COCIS_EXPIRY_LEN + 1];   /* "" when the card never expires */
    char created_at[COCIS_CREATED_LEN + 1];
} cocis_user_t;

/* Fixed-capacity registry. Embedded code avoids dynamic growth here so a
 * malicious or buggy admin command cannot fragment the heap. */
typedef struct {
    cocis_user_t users[COCIS_REGISTRY_MAX];
    size_t count;
} cocis_registry_t;

/** Empty the registry. */
void cocis_registry_init(cocis_registry_t *registry);

/** Find a user by tag UID, or NULL when unknown. */
const cocis_user_t *cocis_registry_find(const cocis_registry_t *registry, const char *uid);

/**
 * Add a user. Fails on a duplicate UID, an empty UID/name, or a full table.
 * The caller keeps ownership of the passed struct (it is copied).
 */
bool cocis_registry_add(cocis_registry_t *registry, const cocis_user_t *user);

/** Remove a user by UID. Returns false when the UID is not present. */
bool cocis_registry_remove(cocis_registry_t *registry, const char *uid);

/** Change a user's status (suspend / reactivate). */
bool cocis_registry_set_status(cocis_registry_t *registry, const char *uid,
                               cocis_user_status_t status);

/** Change a user's console role. */
bool cocis_registry_set_role(cocis_registry_t *registry, const char *uid,
                             cocis_role_t role);

/**
 * Compute the effective status at a given date.
 * A card whose expiry date is before today is EXPIRED even if the stored
 * status still says ACTIVE. This keeps expiry automatic without a timer.
 */
cocis_user_status_t cocis_user_effective_status(const cocis_user_t *user,
                                                int year, int month, int day);

/** Serialise one user as a CSV line. Returns length or -1 on overflow. */
int cocis_user_format_csv(const cocis_user_t *user, char *buf, size_t buf_len);

/** Parse one CSV line (uid,name,role,status,expiry,created). */
bool cocis_user_parse_csv(const char *line, cocis_user_t *user);
