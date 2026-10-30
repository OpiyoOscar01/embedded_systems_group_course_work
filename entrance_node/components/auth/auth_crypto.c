/**
 * @file auth_crypto.c
 * @brief mbedtls PBKDF2 and hardware RNG for credentials.
 * @author Opiyo Oscar
 * @requirements FR-AUTH-01, INN-08
 */

#include "auth_crypto.h"

#include <string.h>

#include "esp_random.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"

bool cocis_auth_pbkdf2_sha256(const char *password,
                              const uint8_t *salt, size_t salt_len,
                              uint32_t iterations,
                              uint8_t *out, size_t out_len)
{
    if (password == NULL || salt == NULL || out == NULL ||
        iterations == 0 || out_len == 0) {
        return false;
    }

    int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                          (const unsigned char *)password,
                                          strlen(password),
                                          salt, salt_len,
                                          iterations,
                                          (uint32_t)out_len, out);
    return rc == 0;
}

void cocis_auth_random_salt(uint8_t salt[COCIS_AUTH_SALT_LEN])
{
    if (salt == NULL) {
        return;
    }
    esp_fill_random(salt, COCIS_AUTH_SALT_LEN);
}
