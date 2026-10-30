/**
 * @file log_store.c
 * @brief Hash-chained CSV writer and verifier.
 * @author Opiyo Oscar
 * @requirements FR-STO-05, FR-STO-06, INN-03
 */

#include "log_store.h"

#include <stdio.h>
#include <string.h>

#include "nvs.h"
#include "storage.h"

#define LOG_NVS_NAMESPACE   "sys"
#define LOG_KEY_ACCESS_HASH "last_hash"
#define LOG_KEY_ACCESS_SEQ  "seq_counter"
#define LOG_KEY_AUDIT_HASH  "last_hash_aud"
#define LOG_KEY_AUDIT_SEQ   "seq_audit"

#define LOG_PATH_MAX        96
#define LOG_LINE_MAX        (COCIS_LOG_RECORD_MAX + (COCIS_HASH_LEN * 4) + 8)

static const uint8_t ZERO_HASH[COCIS_HASH_LEN] = {0};

/* ------------------------------------------------------------------------- */
/* NVS persistence                                                            */
/* ------------------------------------------------------------------------- */

/** Persist one stream head. Failures are logged by the caller as non-fatal. */
static void save_stream(const char *hash_key, const char *seq_key,
                        const cocis_log_stream_t *stream)
{
    nvs_handle_t handle;
    if (nvs_open(LOG_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_blob(handle, hash_key, stream->last_hash, sizeof(stream->last_hash));
    nvs_set_u32(handle, seq_key, stream->seq);
    nvs_commit(handle);
    nvs_close(handle);
}

/** Load one stream head; a missing or short blob leaves the zero genesis. */
static void load_stream(const char *hash_key, const char *seq_key,
                        cocis_log_stream_t *stream)
{
    memset(stream, 0, sizeof(*stream));

    nvs_handle_t handle;
    if (nvs_open(LOG_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    size_t length = sizeof(stream->last_hash);
    if (nvs_get_blob(handle, hash_key, stream->last_hash, &length) == ESP_OK &&
        length == sizeof(stream->last_hash)) {
        stream->have_last = true;
    }

    uint32_t seq = 0;
    if (nvs_get_u32(handle, seq_key, &seq) == ESP_OK) {
        stream->seq = seq;
    }
    nvs_close(handle);
}

esp_err_t cocis_log_store_init(cocis_log_store_t *store)
{
    if (store == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    load_stream(LOG_KEY_ACCESS_HASH, LOG_KEY_ACCESS_SEQ, &store->access);
    load_stream(LOG_KEY_AUDIT_HASH, LOG_KEY_AUDIT_SEQ, &store->audit);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* CSV building                                                               */
/* ------------------------------------------------------------------------- */

/**
 * Append one CSV field, replacing separators that would break the schema.
 * Values never contain a comma because names are local and sanitised here.
 */
static bool append_field(char *buffer, size_t buffer_len, size_t *pos, const char *value)
{
    if (*pos > 0) {
        if (*pos + 1 >= buffer_len) {
            return false;
        }
        buffer[(*pos)++] = ',';
    }

    if (value == NULL) {
        return true;
    }

    for (const char *p = value; *p != '\0'; p++) {
        char c = (*p == ',' || *p == '\n' || *p == '\r') ? ';' : *p;
        if (*pos + 1 >= buffer_len) {
            return false;
        }
        buffer[(*pos)++] = c;
    }
    buffer[*pos] = '\0';
    return true;
}

/** Start a fresh field string. */
static void begin_record(char *buffer, size_t buffer_len, size_t *pos)
{
    *pos = 0;
    if (buffer_len > 0) {
        buffer[0] = '\0';
    }
}

/* ------------------------------------------------------------------------- */
/* Append helpers                                                             */
/* ------------------------------------------------------------------------- */

/** Shared tail: compute the link, append the file and update NVS state. */
static esp_err_t finish_record(cocis_log_stream_t *stream, const char *path,
                               const char *canonical)
{
    const uint8_t *prev = stream->have_last ? stream->last_hash : ZERO_HASH;

    uint8_t hash[COCIS_HASH_LEN];
    if (!cocis_chain_next(cocis_hash_sha256, prev, canonical, hash)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char prev_hex[COCIS_HASH_LEN * 2 + 1];
    char hash_hex[COCIS_HASH_LEN * 2 + 1];
    cocis_hex_encode(prev, COCIS_HASH_LEN, prev_hex);
    cocis_hex_encode(hash, COCIS_HASH_LEN, hash_hex);

    char line[LOG_LINE_MAX];
    int written = snprintf(line, sizeof(line), "%s,%s,%s", canonical, prev_hex, hash_hex);
    if (written <= 0 || (size_t)written >= sizeof(line)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = storage_append(path, line);
    if (err != ESP_OK) {
        return err;
    }

    /* Only advance the head after the record is safely on the card. */
    memcpy(stream->last_hash, hash, sizeof(stream->last_hash));
    stream->have_last = true;
    stream->seq++;
    return ESP_OK;
}

esp_err_t cocis_log_store_access(cocis_log_store_t *store, const char *date,
                                 const char *ts_utc, const char *ts_local,
                                 const char *node, const char *uid, const char *name,
                                 cocis_dir_t direction, cocis_result_t result,
                                 const char *reason)
{
    if (store == NULL || date == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char canonical[COCIS_LOG_RECORD_MAX];
    size_t pos = 0;
    begin_record(canonical, sizeof(canonical), &pos);

    char seq_text[16];
    snprintf(seq_text, sizeof(seq_text), "%lu", (unsigned long)store->access.seq);

    bool ok = append_field(canonical, sizeof(canonical), &pos, seq_text) &&
              append_field(canonical, sizeof(canonical), &pos, ts_utc) &&
              append_field(canonical, sizeof(canonical), &pos, ts_local) &&
              append_field(canonical, sizeof(canonical), &pos, node) &&
              append_field(canonical, sizeof(canonical), &pos, uid) &&
              append_field(canonical, sizeof(canonical), &pos, name) &&
              append_field(canonical, sizeof(canonical), &pos,
                           direction == COCIS_DIR_EXIT ? "OUT" : "IN") &&
              append_field(canonical, sizeof(canonical), &pos, cocis_result_str(result)) &&
              append_field(canonical, sizeof(canonical), &pos, reason);
    if (!ok) {
        return ESP_ERR_INVALID_SIZE;
    }

    char path[LOG_PATH_MAX];
    snprintf(path, sizeof(path), "logs/access/%s.csv", date);

    esp_err_t err = finish_record(&store->access, path, canonical);
    if (err == ESP_OK) {
        save_stream(LOG_KEY_ACCESS_HASH, LOG_KEY_ACCESS_SEQ, &store->access);
    }
    return err;
}

esp_err_t cocis_log_store_audit(cocis_log_store_t *store, const char *date,
                                const char *ts_utc, const char *actor,
                                const char *action, const char *target,
                                const char *old_value, const char *new_value,
                                const char *result)
{
    if (store == NULL || date == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char canonical[COCIS_LOG_RECORD_MAX];
    size_t pos = 0;
    begin_record(canonical, sizeof(canonical), &pos);

    char seq_text[16];
    snprintf(seq_text, sizeof(seq_text), "%lu", (unsigned long)store->audit.seq);

    bool ok = append_field(canonical, sizeof(canonical), &pos, seq_text) &&
              append_field(canonical, sizeof(canonical), &pos, ts_utc) &&
              append_field(canonical, sizeof(canonical), &pos, actor) &&
              append_field(canonical, sizeof(canonical), &pos, action) &&
              append_field(canonical, sizeof(canonical), &pos, target) &&
              append_field(canonical, sizeof(canonical), &pos, old_value) &&
              append_field(canonical, sizeof(canonical), &pos, new_value) &&
              append_field(canonical, sizeof(canonical), &pos, result);
    if (!ok) {
        return ESP_ERR_INVALID_SIZE;
    }

    char path[LOG_PATH_MAX];
    snprintf(path, sizeof(path), "logs/audit/%s.csv", date);

    esp_err_t err = finish_record(&store->audit, path, canonical);
    if (err == ESP_OK) {
        save_stream(LOG_KEY_AUDIT_HASH, LOG_KEY_AUDIT_SEQ, &store->audit);
    }
    return err;
}

/* ------------------------------------------------------------------------- */
/* Verification                                                               */
/* ------------------------------------------------------------------------- */

esp_err_t cocis_log_store_verify(const char *relative_path,
                                 const uint8_t genesis[COCIS_HASH_LEN],
                                 cocis_log_verify_t *out)
{
    if (relative_path == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out->ok = false;
    out->lines = 0;
    out->bad_line = 0;

    /* Read the whole file; the demo logs are small. A production build would
     * stream line by line to bound RAM use. */
    static char buffer[8192];
    size_t length = 0;
    esp_err_t err = storage_read(relative_path, buffer, sizeof(buffer), &length);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t expected[COCIS_HASH_LEN];
    memcpy(expected, genesis != NULL ? genesis : ZERO_HASH, sizeof(expected));

    char *save = NULL;
    char *line = strtok_r(buffer, "\n", &save);
    while (line != NULL) {
        out->lines++;

        size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\r') {
            line[--line_len] = '\0';
        }

        /* Split off the last two fields: prev_hash and hash. */
        char *hash_field = strrchr(line, ',');
        if (hash_field == NULL) {
            out->bad_line = out->lines;
            return ESP_OK;
        }
        *hash_field++ = '\0';

        char *prev_field = strrchr(line, ',');
        if (prev_field == NULL) {
            out->bad_line = out->lines;
            return ESP_OK;
        }
        *prev_field++ = '\0';

        uint8_t prev_stored[COCIS_HASH_LEN];
        uint8_t hash_stored[COCIS_HASH_LEN];
        if (!cocis_hex_decode(prev_field, prev_stored, sizeof(prev_stored)) ||
            !cocis_hex_decode(hash_field, hash_stored, sizeof(hash_stored))) {
            out->bad_line = out->lines;
            return ESP_OK;
        }

        /* The stored previous hash must match the running chain. */
        if (memcmp(prev_stored, expected, sizeof(expected)) != 0) {
            out->bad_line = out->lines;
            return ESP_OK;
        }

        uint8_t computed[COCIS_HASH_LEN];
        if (!cocis_chain_next(cocis_hash_sha256, expected, line, computed) ||
            memcmp(computed, hash_stored, sizeof(computed)) != 0) {
            out->bad_line = out->lines;
            return ESP_OK;
        }

        memcpy(expected, computed, sizeof(expected));
        line = strtok_r(NULL, "\n", &save);
    }

    out->ok = true;
    return ESP_OK;
}
