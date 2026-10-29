/**
 * @file cocis_log_chain.h
 * @brief SHA-256 chained log record builder and verifier.
 * @author Opiyo Oscar
 * @requirements FR-STO-05, INN-03
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COCIS_HASH_LEN        32
#define COCIS_LOG_RECORD_MAX  512

/* Hash provider: writes COCIS_HASH_LEN bytes for data[0..len). */
typedef void (*cocis_hash_fn)(const uint8_t *data, size_t len, uint8_t out[COCIS_HASH_LEN]);

/* One record and the hash stored alongside it. */
typedef struct {
    const char *text;
    uint8_t stored_hash[COCIS_HASH_LEN];
} cocis_log_record_t;

/*
 * Compute hash = H(prev || record). Returns false if the record is too long.
 */
bool cocis_chain_next(cocis_hash_fn hash,
                      const uint8_t prev[COCIS_HASH_LEN],
                      const char *record,
                      uint8_t out[COCIS_HASH_LEN]);

/*
 * Verify a chain starting from genesis_hash.
 * Returns the index of the first record whose recomputed hash does not match
 * its stored hash, or -1 when the whole chain is valid.
 */
int cocis_chain_verify(cocis_hash_fn hash,
                       const uint8_t genesis_hash[COCIS_HASH_LEN],
                       const cocis_log_record_t *records,
                       size_t count);

/* Target provider backed by mbedtls SHA-256. */
void cocis_hash_sha256(const uint8_t *data, size_t len, uint8_t out[COCIS_HASH_LEN]);
