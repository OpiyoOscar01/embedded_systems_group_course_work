/**
 * @file auth_crypto.h
 * @brief Target-side PBKDF2 and salt generation helpers.
 * @author Opiyo Oscar
 * @requirements FR-AUTH-01, INN-08
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "auth.h"

/**
 * PBKDF2-HMAC-SHA256 using mbedtls.
 * Exposed as a cocis_pbkdf2_fn so the pure auth logic can stay portable.
 */
bool cocis_auth_pbkdf2_sha256(const char *password,
                              const uint8_t *salt, size_t salt_len,
                              uint32_t iterations,
                              uint8_t *out, size_t out_len);

/** Fill a salt buffer with cryptographically strong random bytes. */
void cocis_auth_random_salt(uint8_t salt[COCIS_AUTH_SALT_LEN]);
