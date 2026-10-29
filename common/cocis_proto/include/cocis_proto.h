/**
 * @file cocis_proto.h
 * @brief Message envelope codec, MQTT topic builders and event id helpers.
 *
 * Every inter-node message is a compact JSON object with a shared envelope:
 *
 *   {"v":1,"id":"R1-0007-000142","ts":1793088000,
 *    "src":"R1","type":"tele","data":{...}}
 *
 * The codec is deliberately dependency-free (no cJSON) so the exact same code
 * runs on the ESP32 and in host unit tests. Payloads inside "data" are kept as
 * a raw JSON span and parsed by the owning module.
 *
 * @author Opiyo Oscar
 * @requirements FR-SYN-03, FR-DST-02
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COCIS_PROTO_VERSION       1

/* Field limits (bytes, including the terminating NUL). */
#define COCIS_ENVELOPE_ID_MAX     32
#define COCIS_SRC_MAX             8
#define COCIS_TYPE_MAX            24
#define COCIS_TOPIC_MAX           96

/* Number of recently seen ids remembered for duplicate suppression. */
#define COCIS_DEDUPE_SIZE         32

/* Parsed envelope. `data` points into the buffer passed to the decoder. */
typedef struct {
    int version;
    char id[COCIS_ENVELOPE_ID_MAX];
    uint64_t ts;
    char src[COCIS_SRC_MAX];
    char type[COCIS_TYPE_MAX];
    const char *data;
    size_t data_len;
} cocis_envelope_t;

/* Ring buffer of recently processed message ids (idempotency cache). */
typedef struct {
    char ids[COCIS_DEDUPE_SIZE][COCIS_ENVELOPE_ID_MAX];
    size_t count;
    size_t next;
} cocis_dedupe_t;

/* Monotonic event id generator: "{src}-{boot:04u}-{seq:06u}". */
typedef struct {
    char src[COCIS_SRC_MAX];
    uint32_t boot;
    uint32_t seq;
} cocis_event_id_t;

/*
 * Build an id such as "R1-0007-000142".
 * Returns false when the output buffer is too small or arguments are invalid.
 */
bool cocis_proto_make_id(char *out, size_t out_len,
                         const char *src, uint32_t boot, uint32_t seq);

/* Initialise/reset an id generator (seq starts at 0). */
void cocis_event_id_init(cocis_event_id_t *gen, const char *src, uint32_t boot);

/* Produce the next id; returns false on invalid arguments/buffer too small. */
bool cocis_event_id_next(cocis_event_id_t *gen, char *out, size_t out_len);

/*
 * Encode an envelope. `data_json` must be a complete JSON value (usually an
 * object) or NULL for an empty object. Returns the number of bytes written
 * (excluding the NUL) or -1 on error.
 */
int cocis_proto_encode(char *buf, size_t buf_len,
                       const cocis_envelope_t *env, const char *data_json);

/*
 * Decode an envelope. Accepts unknown fields (skipped) and whitespace.
 * Returns false for malformed JSON or a missing/invalid required field.
 * On success `env->data` points inside `json` and is never NULL; when the
 * message has no data field it points at the literal "{}".
 */
bool cocis_proto_decode(const char *json, cocis_envelope_t *env);

/* Join a prefix and a suffix into "{prefix}/{suffix}". */
bool cocis_proto_topic_join(char *buf, size_t buf_len,
                            const char *prefix, const char *suffix);

/* Build "{prefix}/room/{room}/{leaf}". */
bool cocis_proto_topic_room(char *buf, size_t buf_len,
                            const char *prefix, int room, const char *leaf);

/* Build "{prefix}/node/{node}/{leaf}". */
bool cocis_proto_topic_node(char *buf, size_t buf_len,
                            const char *prefix, const char *node, const char *leaf);

/* Reset the duplicate cache. */
void cocis_dedupe_init(cocis_dedupe_t *cache);

/*
 * Record `id` and report whether it was already present.
 * Returns true for a duplicate (the caller should drop the message).
 */
bool cocis_dedupe_seen(cocis_dedupe_t *cache, const char *id);
