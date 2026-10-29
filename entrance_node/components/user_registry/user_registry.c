/**
 * @file user_registry.c
 * @brief Registry operations and CSV helpers, free of ESP-IDF dependencies.
 *
 * Keeping this file pure C lets the access rules be unit-tested on the host.
 *
 * @author Opiyo Oscar
 * @requirements FR-ACC-08, FR-CLI-04
 */

#include "user_registry.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* String <-> enum helpers                                                    */
/* ------------------------------------------------------------------------- */

/** Convert a role name from CSV/console to an enum. */
static bool role_from_string(const char *text, cocis_role_t *role)
{
    if (strcmp(text, "VIEWER") == 0) {
        *role = COCIS_ROLE_VIEWER;
    } else if (strcmp(text, "SECURITY") == 0) {
        *role = COCIS_ROLE_SECURITY;
    } else if (strcmp(text, "ADMIN") == 0) {
        *role = COCIS_ROLE_ADMIN;
    } else {
        return false;
    }
    return true;
}

/** Convert a role enum to the canonical CSV/console name. */
static const char *role_to_string(cocis_role_t role)
{
    switch (role) {
    case COCIS_ROLE_VIEWER:   return "VIEWER";
    case COCIS_ROLE_SECURITY: return "SECURITY";
    case COCIS_ROLE_ADMIN:    return "ADMIN";
    default:                  return NULL;
    }
}

/** Convert a status name from CSV/console to an enum. */
static bool status_from_string(const char *text, cocis_user_status_t *status)
{
    if (strcmp(text, "ACTIVE") == 0) {
        *status = COCIS_USER_ACTIVE;
    } else if (strcmp(text, "SUSPENDED") == 0) {
        *status = COCIS_USER_SUSPENDED;
    } else if (strcmp(text, "EXPIRED") == 0) {
        *status = COCIS_USER_EXPIRED;
    } else {
        return false;
    }
    return true;
}

/** Convert a status enum to the canonical CSV/console name. */
static const char *status_to_string(cocis_user_status_t status)
{
    switch (status) {
    case COCIS_USER_ACTIVE:    return "ACTIVE";
    case COCIS_USER_SUSPENDED: return "SUSPENDED";
    case COCIS_USER_EXPIRED:   return "EXPIRED";
    default:                   return NULL;
    }
}

/* ------------------------------------------------------------------------- */
/* CSV field parsing                                                          */
/* ------------------------------------------------------------------------- */

/**
 * Copy the next comma-separated field into `out`, trimming surrounding spaces.
 * Returns false when the field does not fit or the line ends early.
 */
static bool next_field(const char **cursor, char *out, size_t out_len)
{
    if (*cursor == NULL || **cursor == '\0') {
        return false;
    }

    const char *field_start = *cursor;
    const char *field_end = strchr(field_start, ',');
    if (field_end == NULL) {
        field_end = field_start + strlen(field_start);
    }

    /* Consume the comma (if any) for the next call. */
    const char *next = (*field_end == ',') ? field_end + 1 : field_end;

    /* Trim surrounding spaces so hand-edited CSV files still parse. */
    while (field_start < field_end && *field_start == ' ') {
        field_start++;
    }
    while (field_end > field_start && (field_end[-1] == ' ' || field_end[-1] == '\r' ||
                                       field_end[-1] == '\n')) {
        field_end--;
    }

    size_t length = (size_t)(field_end - field_start);
    if (length + 1 > out_len) {
        return false;
    }

    memcpy(out, field_start, length);
    out[length] = '\0';
    *cursor = next;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Registry operations                                                        */
/* ------------------------------------------------------------------------- */

void cocis_registry_init(cocis_registry_t *registry)
{
    if (registry == NULL) {
        return;
    }
    memset(registry, 0, sizeof(*registry));
}

const cocis_user_t *cocis_registry_find(const cocis_registry_t *registry, const char *uid)
{
    if (registry == NULL || uid == NULL || uid[0] == '\0') {
        return NULL;
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->users[i].uid, uid) == 0) {
            return &registry->users[i];
        }
    }
    return NULL;
}

bool cocis_registry_add(cocis_registry_t *registry, const cocis_user_t *user)
{
    if (registry == NULL || user == NULL) {
        return false;
    }
    if (user->uid[0] == '\0' || user->name[0] == '\0') {
        return false;
    }
    if (!memchr(user->uid, '\0', sizeof(user->uid)) ||
        !memchr(user->name, '\0', sizeof(user->name))) {
        return false;
    }
    if (cocis_registry_find(registry, user->uid) != NULL) {
        return false;
    }
    if (registry->count >= COCIS_REGISTRY_MAX) {
        return false;
    }

    registry->users[registry->count] = *user;
    registry->count++;
    return true;
}

bool cocis_registry_remove(cocis_registry_t *registry, const char *uid)
{
    if (registry == NULL || uid == NULL) {
        return false;
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->users[i].uid, uid) == 0) {
            /* Compact the array by moving the last entry into the gap. */
            registry->users[i] = registry->users[registry->count - 1];
            memset(&registry->users[registry->count - 1], 0, sizeof(cocis_user_t));
            registry->count--;
            return true;
        }
    }
    return false;
}

bool cocis_registry_set_status(cocis_registry_t *registry, const char *uid,
                               cocis_user_status_t status)
{
    if (registry == NULL || uid == NULL) {
        return false;
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->users[i].uid, uid) == 0) {
            registry->users[i].status = status;
            return true;
        }
    }
    return false;
}

bool cocis_registry_set_role(cocis_registry_t *registry, const char *uid,
                             cocis_role_t role)
{
    if (registry == NULL || uid == NULL) {
        return false;
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->users[i].uid, uid) == 0) {
            registry->users[i].role = role;
            return true;
        }
    }
    return false;
}

cocis_user_status_t cocis_user_effective_status(const cocis_user_t *user,
                                                int year, int month, int day)
{
    if (user == NULL) {
        return COCIS_USER_EXPIRED;
    }
    if (user->status == COCIS_USER_SUSPENDED) {
        return COCIS_USER_SUSPENDED;
    }
    if (user->status == COCIS_USER_EXPIRED) {
        return COCIS_USER_EXPIRED;
    }
    if (user->expiry[0] == '\0') {
        return COCIS_USER_ACTIVE;
    }

    /* ISO dates sort lexicographically, so a simple compare is enough. */
    char today[COCIS_EXPIRY_LEN + 1];
    int written = snprintf(today, sizeof(today), "%04d-%02d-%02d", year, month, day);
    if (written != COCIS_EXPIRY_LEN) {
        return COCIS_USER_EXPIRED;
    }

    return (strcmp(user->expiry, today) < 0) ? COCIS_USER_EXPIRED : COCIS_USER_ACTIVE;
}

/* ------------------------------------------------------------------------- */
/* CSV serialisation                                                          */
/* ------------------------------------------------------------------------- */

int cocis_user_format_csv(const cocis_user_t *user, char *buf, size_t buf_len)
{
    if (user == NULL || buf == NULL || buf_len == 0 || user->uid[0] == '\0') {
        return -1;
    }

    const char *role = role_to_string(user->role);
    const char *status = status_to_string(user->status);
    if (role == NULL || status == NULL) {
        return -1;
    }

    int written = snprintf(buf, buf_len, "%s,%s,%s,%s,%s,%s",
                           user->uid, user->name, role, status,
                           user->expiry, user->created_at);
    return (written > 0 && (size_t)written < buf_len) ? written : -1;
}

bool cocis_user_parse_csv(const char *line, cocis_user_t *user)
{
    if (line == NULL || user == NULL) {
        return false;
    }

    memset(user, 0, sizeof(*user));

    const char *cursor = line;
    char role_text[10] = {0};
    char status_text[10] = {0};

    /* The six columns mirror Table 33 of the specification. */
    if (!next_field(&cursor, user->uid, sizeof(user->uid)) ||
        !next_field(&cursor, user->name, sizeof(user->name)) ||
        !next_field(&cursor, role_text, sizeof(role_text)) ||
        !next_field(&cursor, status_text, sizeof(status_text)) ||
        !next_field(&cursor, user->expiry, sizeof(user->expiry)) ||
        !next_field(&cursor, user->created_at, sizeof(user->created_at))) {
        return false;
    }

    return role_from_string(role_text, &user->role) &&
           status_from_string(status_text, &user->status) &&
           user->uid[0] != '\0' && user->name[0] != '\0';
}
