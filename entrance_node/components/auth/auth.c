/**
 * @file auth.c
 * @brief Authentication state machine and lockout policy (pure logic).
 * @author Opiyo Oscar
 * @requirements FR-AUTH-01..05, FR-CLI-02, INN-08
 */

#include "auth.h"

#include <string.h>

/** Millisecond timestamps are 32-bit and wrap; compare with this idiom. */
static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (now_ms - deadline_ms) < 0x80000000u;
}

/** Compare two byte strings without an early exit (constant time). */
static bool constant_time_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t difference = 0;
    for (size_t i = 0; i < len; i++) {
        difference |= (uint8_t)(a[i] ^ b[i]);
    }
    return difference == 0;
}

/** Find a credential index, or -1. */
static int find_index(const cocis_auth_t *auth, const char *username)
{
    if (auth == NULL || username == NULL || username[0] == '\0') {
        return -1;
    }
    for (size_t i = 0; i < auth->count; i++) {
        if (strcmp(auth->users[i].username, username) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/** Derive the candidate hash and compare it with the stored one. */
static bool password_matches(const cocis_credential_t *credential,
                             const char *password, cocis_pbkdf2_fn pbkdf2)
{
    if (pbkdf2 == NULL) {
        return false;
    }

    uint8_t candidate[COCIS_AUTH_HASH_LEN];
    if (!pbkdf2(password, credential->salt, sizeof(credential->salt),
                COCIS_AUTH_ITERATIONS, candidate, sizeof(candidate))) {
        return false;
    }
    return constant_time_equal(candidate, credential->hash, sizeof(candidate));
}

void cocis_auth_init(cocis_auth_t *auth)
{
    if (auth == NULL) {
        return;
    }
    memset(auth, 0, sizeof(*auth));
    auth->session_user = -1;
}

bool cocis_auth_add(cocis_auth_t *auth, const char *username, cocis_role_t role,
                    const uint8_t salt[COCIS_AUTH_SALT_LEN],
                    const uint8_t hash[COCIS_AUTH_HASH_LEN],
                    bool must_change)
{
    if (auth == NULL || username == NULL || username[0] == '\0' ||
        salt == NULL || hash == NULL) {
        return false;
    }
    if (strlen(username) >= COCIS_AUTH_NAME_LEN ||
        find_index(auth, username) >= 0 ||
        auth->count >= COCIS_AUTH_MAX_USERS) {
        return false;
    }
    if (role != COCIS_ROLE_VIEWER && role != COCIS_ROLE_SECURITY &&
        role != COCIS_ROLE_ADMIN) {
        return false;
    }

    cocis_credential_t *credential = &auth->users[auth->count];
    memset(credential, 0, sizeof(*credential));
    strncpy(credential->username, username, sizeof(credential->username) - 1);
    memcpy(credential->salt, salt, sizeof(credential->salt));
    memcpy(credential->hash, hash, sizeof(credential->hash));
    credential->role = role;
    credential->must_change = must_change;
    auth->count++;
    return true;
}

const cocis_credential_t *cocis_auth_find(const cocis_auth_t *auth, const char *username)
{
    int index = find_index(auth, username);
    return index < 0 ? NULL : &auth->users[index];
}

cocis_auth_result_t cocis_auth_login(cocis_auth_t *auth, const char *username,
                                     const char *password, uint32_t now_ms,
                                     cocis_pbkdf2_fn pbkdf2)
{
    if (auth == NULL || username == NULL || password == NULL) {
        return COCIS_AUTH_BAD_CREDENTIALS;
    }

    int index = find_index(auth, username);
    if (index < 0) {
        /* Unknown usernames cannot be locked, but they are still rejected. */
        return COCIS_AUTH_BAD_CREDENTIALS;
    }

    cocis_credential_t *credential = &auth->users[index];

    /* A lock that has expired is cleared before the next attempt. */
    if (credential->locked_until_ms != 0) {
        if (!time_reached(now_ms, credential->locked_until_ms)) {
            return COCIS_AUTH_LOCKED;
        }
        credential->locked_until_ms = 0;
        credential->fails = 0;
    }

    if (password_matches(credential, password, pbkdf2)) {
        credential->fails = 0;
        auth->session_user = index;
        auth->last_activity_ms = now_ms;
        return credential->must_change ? COCIS_AUTH_MUST_CHANGE : COCIS_AUTH_OK;
    }

    /* Failed attempt: count it and lock after three. */
    if (credential->fails < 255) {
        credential->fails++;
    }
    if (credential->fails >= 3) {
        credential->locked_until_ms = now_ms + COCIS_LOCKOUT_S * 1000u;
        return COCIS_AUTH_LOCKED;
    }
    return COCIS_AUTH_BAD_CREDENTIALS;
}

void cocis_auth_logout(cocis_auth_t *auth)
{
    if (auth != NULL) {
        auth->session_user = -1;
        auth->last_activity_ms = 0;
    }
}

bool cocis_auth_session_role(cocis_auth_t *auth, uint32_t now_ms, cocis_role_t *role)
{
    if (auth == NULL || auth->session_user < 0 ||
        (size_t)auth->session_user >= auth->count) {
        return false;
    }

    /* Idle timeout: end the session lazily on the next authenticated call. */
    uint32_t idle_ms = now_ms - auth->last_activity_ms;
    if (idle_ms >= COCIS_SESSION_IDLE_S * 1000u) {
        auth->session_user = -1;
        return false;
    }

    auth->last_activity_ms = now_ms;

    const cocis_credential_t *credential = &auth->users[auth->session_user];
    if (credential->must_change) {
        return false;
    }
    if (role != NULL) {
        *role = credential->role;
    }
    return true;
}

bool cocis_auth_require(cocis_auth_t *auth, uint32_t now_ms, cocis_role_t required)
{
    cocis_role_t current = COCIS_ROLE_VIEWER;
    if (!cocis_auth_session_role(auth, now_ms, &current)) {
        return false;
    }
    return current >= required;
}

cocis_auth_result_t cocis_auth_change_password(cocis_auth_t *auth, const char *username,
                                               const char *old_password,
                                               const char *new_password,
                                               const uint8_t new_salt[COCIS_AUTH_SALT_LEN],
                                               uint32_t now_ms,
                                               cocis_pbkdf2_fn pbkdf2)
{
    if (auth == NULL || username == NULL || old_password == NULL ||
        new_password == NULL || new_salt == NULL || new_password[0] == '\0') {
        return COCIS_AUTH_BAD_CREDENTIALS;
    }

    int index = find_index(auth, username);
    if (index < 0) {
        return COCIS_AUTH_BAD_CREDENTIALS;
    }

    cocis_credential_t *credential = &auth->users[index];
    if (credential->locked_until_ms != 0 && !time_reached(now_ms, credential->locked_until_ms)) {
        return COCIS_AUTH_LOCKED;
    }

    if (!password_matches(credential, old_password, pbkdf2)) {
        if (credential->fails < 255) {
            credential->fails++;
        }
        if (credential->fails >= 3) {
            credential->locked_until_ms = now_ms + COCIS_LOCKOUT_S * 1000u;
            return COCIS_AUTH_LOCKED;
        }
        return COCIS_AUTH_BAD_CREDENTIALS;
    }

    uint8_t new_hash[COCIS_AUTH_HASH_LEN];
    if (!pbkdf2(new_password, new_salt, COCIS_AUTH_SALT_LEN,
                COCIS_AUTH_ITERATIONS, new_hash, sizeof(new_hash))) {
        return COCIS_AUTH_BAD_CREDENTIALS;
    }

    memcpy(credential->salt, new_salt, sizeof(credential->salt));
    memcpy(credential->hash, new_hash, sizeof(credential->hash));
    credential->must_change = false;
    credential->fails = 0;
    credential->locked_until_ms = 0;

    /* Keep the user logged in after a successful change. */
    auth->session_user = index;
    auth->last_activity_ms = now_ms;
    return COCIS_AUTH_OK;
}

const char *cocis_auth_result_str(cocis_auth_result_t result)
{
    switch (result) {
    case COCIS_AUTH_OK:              return "OK";
    case COCIS_AUTH_BAD_CREDENTIALS: return "BAD_CREDENTIALS";
    case COCIS_AUTH_LOCKED:          return "LOCKED";
    case COCIS_AUTH_MUST_CHANGE:     return "MUST_CHANGE_PASSWORD";
    case COCIS_AUTH_NOT_LOGGED_IN:   return "NOT_LOGGED_IN";
    case COCIS_AUTH_FORBIDDEN:       return "FORBIDDEN";
    default:                         return "UNKNOWN";
    }
}
