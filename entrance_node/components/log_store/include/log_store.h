/**
 * @file log_store.h
 * @brief Tamper-evident CSV logging on the SD card.
 *
 * Each record is appended as:
 *   canonical_text,prev_hash,hash
 * where hash = SHA-256(prev_hash || canonical_text). The head of each stream
 * (access and audit) is persisted in NVS so the chain survives reboots.
 *
 * Verification recomputes every link and reports the first bad line.
 *
 * @author Opiyo Oscar
 * @requirements FR-STO-05, FR-STO-06, INN-03
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cocis_log_chain.h"
#include "cocis_types.h"
#include "esp_err.h"

typedef struct {
    uint8_t last_hash[COCIS_HASH_LEN];
    bool have_last;
    uint32_t seq;
} cocis_log_stream_t;

typedef struct {
    cocis_log_stream_t access;
    cocis_log_stream_t audit;
} cocis_log_store_t;

typedef struct {
    bool ok;
    size_t lines;
    size_t bad_line;      /* 1-based line number of the first mismatch */
} cocis_log_verify_t;

/** Load the chain heads from NVS. Safe to call before the SD card is ready. */
esp_err_t cocis_log_store_init(cocis_log_store_t *store);

/** Append one access record; `date` is "YYYYMMDD". */
esp_err_t cocis_log_store_access(cocis_log_store_t *store, const char *date,
                                 const char *ts_utc, const char *ts_local,
                                 const char *node, const char *uid, const char *name,
                                 cocis_dir_t direction, cocis_result_t result,
                                 const char *reason);

/** Append one audit record; `date` is "YYYYMMDD". */
esp_err_t cocis_log_store_audit(cocis_log_store_t *store, const char *date,
                                const char *ts_utc, const char *actor,
                                const char *action, const char *target,
                                const char *old_value, const char *new_value,
                                const char *result);

/**
 * Verify a log file against its chain.
 * `genesis` may be NULL to use 32 zero bytes as the first previous hash.
 */
esp_err_t cocis_log_store_verify(const char *relative_path,
                                 const uint8_t genesis[COCIS_HASH_LEN],
                                 cocis_log_verify_t *out);
