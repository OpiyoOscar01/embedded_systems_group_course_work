/**
 * @file chain.c
 * @brief Hash-chain logic, free of hardware and crypto library dependencies.
 * @author Opiyo Oscar
 * @requirements FR-STO-05, INN-03
 */

#include "cocis_log_chain.h"

#include <string.h>

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
