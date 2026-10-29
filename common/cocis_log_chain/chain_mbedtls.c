/**
 * @file chain_mbedtls.c
 * @brief Target SHA-256 provider using mbedtls.
 * @author Opiyo Oscar
 * @requirements FR-STO-05, INN-03
 */

#include "cocis_log_chain.h"

#include "mbedtls/sha256.h"

void cocis_hash_sha256(const uint8_t *data, size_t len, uint8_t out[COCIS_HASH_LEN])
{
    mbedtls_sha256(data, len, out, 0);
}
