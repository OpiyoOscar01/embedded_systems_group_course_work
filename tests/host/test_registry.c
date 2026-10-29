/**
 * @file test_registry.c
 * @brief Host tests for the user registry and CSV helpers.
 * @author Opiyo Oscar
 * @requirements FR-ACC-08, FR-CLI-04
 */

#include "user_registry.h"
#include "test_util.h"

#include <string.h>

/** Build a valid active user for tests. */
static void make_user(cocis_user_t *user, const char *uid, const char *name,
                      cocis_role_t role)
{
    memset(user, 0, sizeof(*user));
    snprintf(user->uid, sizeof(user->uid), "%s", uid);
    snprintf(user->name, sizeof(user->name), "%s", name);
    user->role = role;
    user->status = COCIS_USER_ACTIVE;
    snprintf(user->created_at, sizeof(user->created_at), "2026-10-29T10:00:00");
}

void test_registry_basic(void)
{
    cocis_registry_t reg;
    cocis_registry_init(&reg);
    CHECK_EQ(reg.count, 0);
    CHECK(cocis_registry_find(&reg, "00112233") == NULL);

    cocis_user_t alice;
    make_user(&alice, "00112233", "Alice", COCIS_ROLE_ADMIN);
    CHECK(cocis_registry_add(&reg, &alice));
    CHECK_EQ(reg.count, 1);

    const cocis_user_t *found = cocis_registry_find(&reg, "00112233");
    CHECK(found != NULL);
    CHECK(strcmp(found->name, "Alice") == 0);
    CHECK_EQ(found->role, COCIS_ROLE_ADMIN);

    /* Duplicate UIDs and empty identities are rejected. */
    CHECK(!cocis_registry_add(&reg, &alice));
    cocis_user_t bad;
    make_user(&bad, "", "Nobody", COCIS_ROLE_VIEWER);
    CHECK(!cocis_registry_add(&reg, &bad));
    make_user(&bad, "9999", "", COCIS_ROLE_VIEWER);
    CHECK(!cocis_registry_add(&reg, &bad));

    CHECK(cocis_registry_set_status(&reg, "00112233", COCIS_USER_SUSPENDED));
    CHECK_EQ(cocis_registry_find(&reg, "00112233")->status, COCIS_USER_SUSPENDED);
    CHECK(!cocis_registry_set_status(&reg, "missing", COCIS_USER_ACTIVE));

    CHECK(cocis_registry_set_role(&reg, "00112233", COCIS_ROLE_SECURITY));
    CHECK_EQ(cocis_registry_find(&reg, "00112233")->role, COCIS_ROLE_SECURITY);
    CHECK(!cocis_registry_set_role(&reg, "missing", COCIS_ROLE_ADMIN));

    CHECK(cocis_registry_remove(&reg, "00112233"));
    CHECK(cocis_registry_find(&reg, "00112233") == NULL);
    CHECK(!cocis_registry_remove(&reg, "00112233"));
}

void test_registry_capacity(void)
{
    cocis_registry_t reg;
    cocis_registry_init(&reg);

    for (int i = 0; i < COCIS_REGISTRY_MAX; i++) {
        cocis_user_t user;
        char uid[16];
        snprintf(uid, sizeof(uid), "U%04d", i);
        make_user(&user, uid, "User", COCIS_ROLE_VIEWER);
        CHECK(cocis_registry_add(&reg, &user));
    }
    CHECK_EQ(reg.count, COCIS_REGISTRY_MAX);

    cocis_user_t extra;
    make_user(&extra, "OVERFLOW", "Extra", COCIS_ROLE_VIEWER);
    CHECK(!cocis_registry_add(&reg, &extra));

    /* Removing one frees exactly one slot. */
    CHECK(cocis_registry_remove(&reg, "U0000"));
    CHECK(cocis_registry_add(&reg, &extra));
    CHECK_EQ(reg.count, COCIS_REGISTRY_MAX);
}

void test_registry_expiry(void)
{
    cocis_user_t user;
    make_user(&user, "ABCD", "Bob", COCIS_ROLE_VIEWER);

    /* No expiry date means the card never ages out. */
    CHECK_EQ(cocis_user_effective_status(&user, 2030, 1, 1), COCIS_USER_ACTIVE);

    snprintf(user.expiry, sizeof(user.expiry), "2026-10-29");
    CHECK_EQ(cocis_user_effective_status(&user, 2026, 10, 28), COCIS_USER_ACTIVE);
    CHECK_EQ(cocis_user_effective_status(&user, 2026, 10, 29), COCIS_USER_ACTIVE);
    CHECK_EQ(cocis_user_effective_status(&user, 2026, 10, 30), COCIS_USER_EXPIRED);

    user.status = COCIS_USER_SUSPENDED;
    CHECK_EQ(cocis_user_effective_status(&user, 2026, 1, 1), COCIS_USER_SUSPENDED);

    user.status = COCIS_USER_EXPIRED;
    CHECK_EQ(cocis_user_effective_status(&user, 2026, 1, 1), COCIS_USER_EXPIRED);

    CHECK_EQ(cocis_user_effective_status(NULL, 2026, 1, 1), COCIS_USER_EXPIRED);
}

void test_registry_csv(void)
{
    cocis_user_t user;
    make_user(&user, "A1B2C3", "Visitor One", COCIS_ROLE_SECURITY);
    snprintf(user.expiry, sizeof(user.expiry), "2026-12-31");

    char line[160];
    int len = cocis_user_format_csv(&user, line, sizeof(line));
    CHECK(len > 0);
    CHECK(strcmp(line, "A1B2C3,Visitor One,SECURITY,ACTIVE,2026-12-31,"
                       "2026-10-29T10:00:00") == 0);

    cocis_user_t parsed;
    CHECK(cocis_user_parse_csv(line, &parsed));
    CHECK(strcmp(parsed.uid, user.uid) == 0);
    CHECK(strcmp(parsed.name, user.name) == 0);
    CHECK_EQ(parsed.role, COCIS_ROLE_SECURITY);
    CHECK_EQ(parsed.status, COCIS_USER_ACTIVE);
    CHECK(strcmp(parsed.expiry, "2026-12-31") == 0);
    CHECK(strcmp(parsed.created_at, "2026-10-29T10:00:00") == 0);

    /* Spaces around fields are tolerated. */
    CHECK(cocis_user_parse_csv(" A1 , Visitor , VIEWER , ACTIVE , , 2026-10-29T10:00:00 ",
                               &parsed));
    CHECK(strcmp(parsed.uid, "A1") == 0);
    CHECK(strcmp(parsed.name, "Visitor") == 0);
    CHECK(strcmp(parsed.expiry, "") == 0);

    /* Malformed lines are rejected. */
    CHECK(!cocis_user_parse_csv("only,three,fields", &parsed));
    CHECK(!cocis_user_parse_csv("A1,Name,SUPERUSER,ACTIVE,,2026-10-29T10:00:00", &parsed));
    CHECK(!cocis_user_parse_csv("A1,Name,ADMIN,NOWHERE,,2026-10-29T10:00:00", &parsed));
    CHECK(!cocis_user_parse_csv("", &parsed));
    CHECK(!cocis_user_parse_csv(NULL, &parsed));

    /* A too-small output buffer reports an error instead of truncating. */
    CHECK_EQ(cocis_user_format_csv(&user, line, 8), -1);
    CHECK_EQ(cocis_user_format_csv(NULL, line, sizeof(line)), -1);
}
