/**
 * @file chain.c
 * @brief Hash-chain logic, free of hardware and crypto library dependencies.
 * @author Opiyo Oscar
 * @requirements FR-STO-05, INN-03
 */

#include "cocis_log_chain.h"

#include <string.h>

/** Convert one nibble to a lowercase hex character. */
static char nibble_to_hex(uint8_t value)
{
    return (value < 10) ? (char)('0' + value) : (char)('a' + (value - 10));
}

/** Convert one hex character to a nibble, or -1 when invalid. */
static int hex_to_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

/**
 * Compute the next link: hash(prev_hash || record_text).
 *
 * The previous hash is always COCIS_HASH_LEN bytes; the record is the exact
 * CSV line that is written to disk. Concatenating the two into a temporary
 * buffer gives the same result as streaming them into the hash function but
 * keeps this module independent of any crypto library.
 */
bool cocis_chain_next(cocis_hash_fn hash,
                      const uint8_t prev[COCIS_HASH_LEN],
                      const char *record,
                      uint8_t out[COCIS_HASH_LEN])
{
    if (hash == NULL || prev == NULL || record == NULL || out == NULL) {
        return false;
    }

    size_t record_len = strlen(record);
    if (record_len > COCIS_LOG_RECORD_MAX) {
        return false;
    }

    uint8_t buffer[COCIS_HASH_LEN + COCIS_LOG_RECORD_MAX];
    memcpy(buffer, prev, COCIS_HASH_LEN);
    memcpy(buffer + COCIS_HASH_LEN, record, record_len);

    hash(buffer, COCIS_HASH_LEN + record_len, out);
    return true;
}

/**
 * Recompute the whole chain from the genesis hash and compare every link with
 * the hash that was stored next to the record.
 *
 * The first mismatch is enough to prove tampering, so verification stops and
 * returns that index. -1 means every record matched.
 */
void cocis_hex_encode(const uint8_t *in, size_t len, char *out)
{
    if (in == NULL || out == NULL) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = nibble_to_hex((uint8_t)(in[i] >> 4));
        out[i * 2 + 1] = nibble_to_hex((uint8_t)(in[i] & 0x0F));
    }
    out[len * 2] = '\0';
}

bool cocis_hex_decode(const char *in, uint8_t *out, size_t len)
{
    if (in == NULL || out == NULL) {
        return false;
    }
    if (strlen(in) != len * 2) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        int high = hex_to_nibble(in[i * 2]);
        int low = hex_to_nibble(in[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

int cocis_chain_verify(cocis_hash_fn hash,
                       const uint8_t genesis_hash[COCIS_HASH_LEN],
                       const cocis_log_record_t *records,
                       size_t count)
{
    if (hash == NULL || genesis_hash == NULL || (records == NULL && count > 0)) {
        return 0;
    }

    uint8_t prev[COCIS_HASH_LEN];
    memcpy(prev, genesis_hash, COCIS_HASH_LEN);

    for (size_t i = 0; i < count; i++) {
        uint8_t computed[COCIS_HASH_LEN];
        if (!cocis_chain_next(hash, prev, records[i].text, computed)) {
            return (int)i;
        }
        if (memcmp(computed, records[i].stored_hash, COCIS_HASH_LEN) != 0) {
            return (int)i;
        }
        memcpy(prev, computed, COCIS_HASH_LEN);
    }

    return -1;
}
