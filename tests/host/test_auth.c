/**
 * @file test_auth.c
 * @brief Host tests for authentication, lockout, roles and idle sessions.
 * @author Opiyo Oscar
 * @requirements FR-AUTH-01..05, FR-CLI-02
 */

#include "auth.h"
#include "test_util.h"

#include <string.h>

/**
 * Fast deterministic PBKDF2 stand-in.
 * The real target uses mbedtls; this stub lets the state machine be tested
 * without pulling crypto into the host build. It still enforces the minimum
 * iteration count so a regression there is caught.
 */
static bool fake_pbkdf2(const char *password, const uint8_t *salt, size_t salt_len,
                        uint32_t iterations, uint8_t *out, size_t out_len)
{
    if (iterations < COCIS_AUTH_ITERATIONS || password == NULL ||
        salt == NULL || out == NULL) {
        return false;
    }

    uint32_t h = 2166136261u ^ iterations;
    for (size_t i = 0; i < strlen(password); i++) {
        h ^= (uint8_t)password[i];
        h *= 16777619u;
    }
    for (size_t i = 0; i < salt_len; i++) {
        h ^= salt[i];
        h *= 16777619u;
    }
    for (size_t i = 0; i < out_len; i++) {
        out[i] = (uint8_t)(h >> ((i % 4) * 8)) ^ (uint8_t)(i * 17u);
    }
    return true;
}

/** Add a credential whose salt is derived from `seed`. */
static void add_user(cocis_auth_t *auth, const char *name, const char *password,
                     cocis_role_t role, bool must_change, uint8_t seed)
{
    uint8_t salt[COCIS_AUTH_SALT_LEN];
    uint8_t hash[COCIS_AUTH_HASH_LEN];
    for (size_t i = 0; i < sizeof(salt); i++) {
        salt[i] = (uint8_t)(seed + i);
    }
    CHECK(fake_pbkdf2(password, salt, sizeof(salt), COCIS_AUTH_ITERATIONS,
                      hash, sizeof(hash)));
    CHECK(cocis_auth_add(auth, name, role, salt, hash, must_change));
}

void test_auth_login(void)
{
    cocis_auth_t auth;
    cocis_auth_init(&auth);
    CHECK(auth.session_user == -1);
    CHECK(cocis_auth_find(&auth, "admin") == NULL);

    add_user(&auth, "admin", "secret", COCIS_ROLE_ADMIN, false, 1);

    CHECK_EQ(cocis_auth_login(&auth, "admin", "wrong", 0, fake_pbkdf2),
             COCIS_AUTH_BAD_CREDENTIALS);
    CHECK_EQ(cocis_auth_login(&auth, "admin", "secret", 0, fake_pbkdf2), COCIS_AUTH_OK);

    cocis_role_t role = COCIS_ROLE_VIEWER;
    CHECK(cocis_auth_session_role(&auth, 1000, &role));
    CHECK_EQ(role, COCIS_ROLE_ADMIN);
    CHECK(cocis_auth_require(&auth, 1000, COCIS_ROLE_VIEWER));
    CHECK(cocis_auth_require(&auth, 1000, COCIS_ROLE_ADMIN));

    /* Unknown users never authenticate. */
    CHECK_EQ(cocis_auth_login(&auth, "nobody", "secret", 0, fake_pbkdf2),
             COCIS_AUTH_BAD_CREDENTIALS);

    cocis_auth_logout(&auth);
    CHECK(!cocis_auth_require(&auth, 0, COCIS_ROLE_VIEWER));
}

void test_auth_lockout(void)
{
    cocis_auth_t auth;
    cocis_auth_init(&auth);
    add_user(&auth, "admin", "secret", COCIS_ROLE_ADMIN, false, 2);

    CHECK_EQ(cocis_auth_login(&auth, "admin", "bad", 0, fake_pbkdf2),
             COCIS_AUTH_BAD_CREDENTIALS);
    CHECK_EQ(cocis_auth_login(&auth, "admin", "bad", 0, fake_pbkdf2),
             COCIS_AUTH_BAD_CREDENTIALS);

    /* The third failure locks the account. */
    CHECK_EQ(cocis_auth_login(&auth, "admin", "bad", 0, fake_pbkdf2), COCIS_AUTH_LOCKED);
    CHECK_EQ(cocis_auth_login(&auth, "admin", "secret", 0, fake_pbkdf2), COCIS_AUTH_LOCKED);

    /* Still locked one millisecond before the 60 s deadline. */
    CHECK_EQ(cocis_auth_login(&auth, "admin", "secret", 59999, fake_pbkdf2),
             COCIS_AUTH_LOCKED);

    /* The deadline clears the lock and the correct password works. */
    CHECK_EQ(cocis_auth_login(&auth, "admin", "secret", 60000, fake_pbkdf2), COCIS_AUTH_OK);
}

void test_auth_idle_and_roles(void)
{
    cocis_auth_t auth;
    cocis_auth_init(&auth);
    add_user(&auth, "security", "pw1", COCIS_ROLE_SECURITY, false, 3);
    add_user(&auth, "viewer", "pw2", COCIS_ROLE_VIEWER, false, 4);

    CHECK_EQ(cocis_auth_login(&auth, "security", "pw1", 0, fake_pbkdf2), COCIS_AUTH_OK);
    CHECK(cocis_auth_require(&auth, 1000, COCIS_ROLE_SECURITY));
    CHECK(!cocis_auth_require(&auth, 1000, COCIS_ROLE_ADMIN));

    /* Activity at 119.999 s keeps the session alive. */
    CHECK(cocis_auth_require(&auth, 119999, COCIS_ROLE_VIEWER));

    /* 120 s after the last activity the session is gone. */
    CHECK(!cocis_auth_require(&auth, 239999, COCIS_ROLE_VIEWER));
    CHECK(!cocis_auth_session_role(&auth, 239999, NULL));
    CHECK(auth.session_user == -1);

    CHECK_EQ(cocis_auth_login(&auth, "viewer", "pw2", 0, fake_pbkdf2), COCIS_AUTH_OK);
    CHECK(cocis_auth_require(&auth, 1, COCIS_ROLE_VIEWER));
    CHECK(!cocis_auth_require(&auth, 1, COCIS_ROLE_SECURITY));
}

void test_auth_must_change(void)
{
    cocis_auth_t auth;
    cocis_auth_init(&auth);
    add_user(&auth, "admin", "one-time", COCIS_ROLE_ADMIN, true, 5);

    CHECK_EQ(cocis_auth_login(&auth, "admin", "one-time", 0, fake_pbkdf2),
             COCIS_AUTH_MUST_CHANGE);

    /* Until the password is changed, no command may run. */
    CHECK(!cocis_auth_require(&auth, 0, COCIS_ROLE_VIEWER));
    CHECK(!cocis_auth_session_role(&auth, 0, NULL));

    /* Wrong old password is rejected and counted. */
    uint8_t salt[COCIS_AUTH_SALT_LEN];
    for (size_t i = 0; i < sizeof(salt); i++) {
        salt[i] = (uint8_t)(100 + i);
    }
    CHECK_EQ(cocis_auth_change_password(&auth, "admin", "wrong", "new-pass",
                                        salt, 0, fake_pbkdf2),
             COCIS_AUTH_BAD_CREDENTIALS);

    CHECK_EQ(cocis_auth_change_password(&auth, "admin", "one-time", "new-pass",
                                        salt, 0, fake_pbkdf2),
             COCIS_AUTH_OK);
    CHECK(!cocis_auth_find(&auth, "admin")->must_change);

    /* The new password works and the old one no longer does. */
    cocis_auth_logout(&auth);
    CHECK_EQ(cocis_auth_login(&auth, "admin", "new-pass", 1, fake_pbkdf2),
             COCIS_AUTH_OK);
    cocis_auth_logout(&auth);
    CHECK_EQ(cocis_auth_login(&auth, "admin", "one-time", 1, fake_pbkdf2),
             COCIS_AUTH_BAD_CREDENTIALS);
}

void test_auth_limits(void)
{
    cocis_auth_t auth;
    cocis_auth_init(&auth);

    add_user(&auth, "u1", "a", COCIS_ROLE_VIEWER, false, 10);
    CHECK(!cocis_auth_add(&auth, "u1", COCIS_ROLE_VIEWER,
                          auth.users[0].salt, auth.users[0].hash, false));

    add_user(&auth, "u2", "b", COCIS_ROLE_VIEWER, false, 20);
    add_user(&auth, "u3", "c", COCIS_ROLE_VIEWER, false, 30);
    add_user(&auth, "u4", "d", COCIS_ROLE_VIEWER, false, 40);

    uint8_t salt[COCIS_AUTH_SALT_LEN] = {0};
    uint8_t hash[COCIS_AUTH_HASH_LEN] = {0};
    CHECK(!cocis_auth_add(&auth, "u5", COCIS_ROLE_VIEWER, salt, hash, false));

    CHECK(strcmp(cocis_auth_result_str(COCIS_AUTH_LOCKED), "LOCKED") == 0);
    CHECK(strcmp(cocis_auth_result_str(COCIS_AUTH_MUST_CHANGE),
                 "MUST_CHANGE_PASSWORD") == 0);
}
