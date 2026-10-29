/**
 * @file proto.c
 * @brief Dependency-free JSON envelope codec, topic helpers and dedupe cache.
 *
 * Design notes for reviewers:
 * - We do not pull in cJSON: the envelope is a fixed, flat object, and keeping
 *   the codec dependency-free lets the same file run under the host test
 *   compiler and on the ESP32.
 * - The "data" member is captured as a raw JSON span. Each feature module
 *   parses only its own payload, which keeps this shared code stable.
 * - All buffers are caller-owned and fixed-size; there is no heap allocation.
 *
 * @author Opiyo Oscar
 * @requirements FR-SYN-03, FR-DST-02
 */

#include "cocis_proto.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Small text helpers                                                         */
/* ------------------------------------------------------------------------- */

/** Advance past JSON whitespace (space, tab, CR, LF). */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

/**
 * Append formatted text to a bounded buffer.
 * `*pos` is the current write offset; the buffer always stays NUL terminated.
 */
static bool append_fmt(char *buf, size_t buf_len, size_t *pos, const char *fmt, ...)
{
    if (*pos >= buf_len) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf + *pos, buf_len - *pos, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= buf_len - *pos) {
        return false;
    }
    *pos += (size_t)written;
    return true;
}

/** Append a JSON string literal with the usual escapes. */
static bool append_json_string(char *buf, size_t buf_len, size_t *pos, const char *text)
{
    if (!append_fmt(buf, buf_len, pos, "\"")) {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        switch (*p) {
        case '"':  if (!append_fmt(buf, buf_len, pos, "\\\"")) return false; break;
        case '\\': if (!append_fmt(buf, buf_len, pos, "\\\\")) return false; break;
        case '\b': if (!append_fmt(buf, buf_len, pos, "\\b")) return false; break;
        case '\f': if (!append_fmt(buf, buf_len, pos, "\\f")) return false; break;
        case '\n': if (!append_fmt(buf, buf_len, pos, "\\n")) return false; break;
        case '\r': if (!append_fmt(buf, buf_len, pos, "\\r")) return false; break;
        case '\t': if (!append_fmt(buf, buf_len, pos, "\\t")) return false; break;
        default:
            if (*p < 0x20) {
                if (!append_fmt(buf, buf_len, pos, "\\u%04x", *p)) return false;
            } else if (!append_fmt(buf, buf_len, pos, "%c", *p)) {
                return false;
            }
            break;
        }
    }

    return append_fmt(buf, buf_len, pos, "\"");
}

/** True when the fixed-size string field is properly NUL terminated. */
static bool field_terminated(const char *field, size_t size)
{
    return memchr(field, '\0', size) != NULL;
}

/* ------------------------------------------------------------------------- */
/* Event ids                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * Format "{src}-{boot:04u}-{seq:06u}".
 * The boot counter makes ids unique across reboots; the sequence makes them
 * unique within a boot, which is exactly what the dedupe cache needs.
 */
bool cocis_proto_make_id(char *out, size_t out_len,
                         const char *src, uint32_t boot, uint32_t seq)
{
    if (out == NULL || out_len == 0 || src == NULL || src[0] == '\0') {
        return false;
    }

    int written = snprintf(out, out_len, "%s-%04u-%06u", src, boot, seq);
    return written > 0 && (size_t)written < out_len;
}

/** Reset a generator and copy the node source id safely into its buffer. */
void cocis_event_id_init(cocis_event_id_t *gen, const char *src, uint32_t boot)
{
    if (gen == NULL) {
        return;
    }

    memset(gen, 0, sizeof(*gen));
    if (src != NULL) {
        snprintf(gen->src, sizeof(gen->src), "%s", src);
    }
    gen->boot = boot;
    gen->seq = 0;
}

/**
 * Produce the next id and advance the sequence.
 * The sequence is only advanced after a successful format, so a too-small
 * buffer does not create a gap in the id stream.
 */
bool cocis_event_id_next(cocis_event_id_t *gen, char *out, size_t out_len)
{
    if (gen == NULL || gen->src[0] == '\0') {
        return false;
    }
    if (!cocis_proto_make_id(out, out_len, gen->src, gen->boot, gen->seq)) {
        return false;
    }
    gen->seq++;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Encoding                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * Serialise an envelope into the caller's buffer.
 *
 * Field order is fixed (v, id, ts, src, type, data) so logs and tests can
 * compare payloads deterministically. The payload is inserted verbatim after
 * the caller has built it; it is the caller's responsibility to pass valid
 * JSON. Returns -1 rather than truncating.
 */
int cocis_proto_encode(char *buf, size_t buf_len,
                       const cocis_envelope_t *env, const char *data_json)
{
    if (buf == NULL || buf_len == 0 || env == NULL || env->version <= 0) {
        return -1;
    }
    if (!field_terminated(env->id, sizeof(env->id)) ||
        !field_terminated(env->src, sizeof(env->src)) ||
        !field_terminated(env->type, sizeof(env->type))) {
        return -1;
    }
    if (env->id[0] == '\0' || env->src[0] == '\0' || env->type[0] == '\0') {
        return -1;
    }

    size_t pos = 0;
    bool ok = append_fmt(buf, buf_len, &pos, "{\"v\":%d,\"id\":", env->version) &&
              append_json_string(buf, buf_len, &pos, env->id) &&
              append_fmt(buf, buf_len, &pos, ",\"ts\":%llu,\"src\":",
                         (unsigned long long)env->ts) &&
              append_json_string(buf, buf_len, &pos, env->src) &&
              append_fmt(buf, buf_len, &pos, ",\"type\":") &&
              append_json_string(buf, buf_len, &pos, env->type) &&
              append_fmt(buf, buf_len, &pos, ",\"data\":");

    if (!ok) {
        return -1;
    }

    /* The caller guarantees data_json is valid JSON; NULL means "no payload". */
    if (data_json == NULL || data_json[0] == '\0') {
        ok = append_fmt(buf, buf_len, &pos, "{}");
    } else {
        ok = append_fmt(buf, buf_len, &pos, "%s", data_json);
    }
    if (!ok || !append_fmt(buf, buf_len, &pos, "}")) {
        return -1;
    }

    return (int)pos;
}

/* ------------------------------------------------------------------------- */
/* Decoding                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * Parse a JSON string. When `out` is NULL the string is validated and skipped.
 * Supports the standard short escapes; \uXXXX is reduced to '?' because the
 * protocol only carries ASCII identifiers.
 */
static bool parse_json_string(const char **cursor, char *out, size_t out_len)
{
    const char *p = *cursor;
    if (*p != '"') {
        return false;
    }
    p++;

    size_t count = 0;
    while (*p != '"') {
        if (*p == '\0') {
            return false;
        }

        unsigned char value = (unsigned char)*p;
        if (value == '\\') {
            p++;
            switch (*p) {
            case '"':  value = '"';  break;
            case '\\': value = '\\'; break;
            case '/':  value = '/';  break;
            case 'b':  value = '\b'; break;
            case 'f':  value = '\f'; break;
            case 'n':  value = '\n'; break;
            case 'r':  value = '\r'; break;
            case 't':  value = '\t'; break;
            case 'u': {
                for (int i = 1; i <= 4; i++) {
                    char c = p[i];
                    bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                               (c >= 'A' && c <= 'F');
                    if (!hex) {
                        return false;
                    }
                }
                p += 4;
                value = '?';
                break;
            }
            default:
                return false;
            }
        }

        if (out != NULL) {
            if (count + 1 >= out_len) {
                return false;
            }
            out[count] = (char)value;
        }
        count++;
        p++;
    }

    if (out != NULL) {
        out[count] = '\0';
    }
    *cursor = p + 1;
    return true;
}

/** Parse an unsigned integer (digits only). */
static bool parse_uint(const char **cursor, uint64_t *out)
{
    const char *p = *cursor;
    if (*p < '0' || *p > '9') {
        return false;
    }

    uint64_t value = 0;
    while (*p >= '0' && *p <= '9') {
        uint64_t digit = (uint64_t)(*p - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        p++;
    }

    *cursor = p;
    *out = value;
    return true;
}

/**
 * Capture one JSON value without interpreting it.
 * For objects/arrays the matching closing bracket is found while respecting
 * strings; scalars run to the next structural character.
 */
static bool capture_value(const char **cursor, const char **start, size_t *length)
{
    const char *p = skip_ws(*cursor);
    *start = p;

    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        int depth = 0;
        bool in_string = false;
        bool escaped = false;

        for (; *p != '\0'; p++) {
            char c = *p;
            if (in_string) {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    in_string = false;
                }
                continue;
            }

            if (c == '"') {
                in_string = true;
            } else if (c == open) {
                depth++;
            } else if (c == close) {
                depth--;
                if (depth == 0) {
                    p++;
                    break;
                }
            }
        }

        if (depth != 0) {
            return false;
        }
        *length = (size_t)(p - *start);
        *cursor = p;
        return true;
    }

    if (*p == '"') {
        const char *end = p;
        if (!parse_json_string(&end, NULL, 0)) {
            return false;
        }
        *length = (size_t)(end - p);
        *cursor = end;
        return true;
    }

    /* true / false / null / number */
    const char *q = p;
    while (*q != '\0' && *q != ',' && *q != '}' && *q != ']') {
        q++;
    }
    const char *end = q;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\n' || end[-1] == '\r')) {
        end--;
    }
    if (end == p) {
        return false;
    }

    *length = (size_t)(end - p);
    *cursor = end;
    return true;
}

/**
 * Parse and validate an envelope.
 *
 * Required fields are v, ts, id, src and type. Unknown fields are skipped so
 * newer senders can add metadata without breaking older receivers. The data
 * payload is returned as a span into `json`; it is not copied.
 */
bool cocis_proto_decode(const char *json, cocis_envelope_t *env)
{
    if (json == NULL || env == NULL) {
        return false;
    }

    memset(env, 0, sizeof(*env));
    env->data = NULL;
    env->data_len = 0;

    const char *p = skip_ws(json);
    if (*p != '{') {
        return false;
    }
    p++;

    bool have_data = false;
    bool have_ts = false;
    while (true) {
        p = skip_ws(p);
        if (*p == '}') {
            p++;
            break;
        }
        if (*p != '"') {
            return false;
        }

        char key[16];
        if (!parse_json_string(&p, key, sizeof(key))) {
            return false;
        }

        p = skip_ws(p);
        if (*p != ':') {
            return false;
        }
        p++;
        p = skip_ws(p);

        if (strcmp(key, "v") == 0) {
            uint64_t value = 0;
            if (!parse_uint(&p, &value) || value > 1000) {
                return false;
            }
            env->version = (int)value;
        } else if (strcmp(key, "ts") == 0) {
            if (!parse_uint(&p, &env->ts)) {
                return false;
            }
            have_ts = true;
        } else if (strcmp(key, "id") == 0) {
            if (!parse_json_string(&p, env->id, sizeof(env->id))) {
                return false;
            }
        } else if (strcmp(key, "src") == 0) {
            if (!parse_json_string(&p, env->src, sizeof(env->src))) {
                return false;
            }
        } else if (strcmp(key, "type") == 0) {
            if (!parse_json_string(&p, env->type, sizeof(env->type))) {
                return false;
            }
        } else if (strcmp(key, "data") == 0) {
            const char *value_start = NULL;
            size_t value_len = 0;
            if (!capture_value(&p, &value_start, &value_len)) {
                return false;
            }
            env->data = value_start;
            env->data_len = value_len;
            have_data = true;
        } else {
            const char *ignored = NULL;
            size_t ignored_len = 0;
            if (!capture_value(&p, &ignored, &ignored_len)) {
                return false;
            }
        }

        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }
        return false;
    }

    p = skip_ws(p);
    if (*p != '\0') {
        return false;
    }

    if (env->version <= 0 || !have_ts || env->id[0] == '\0' ||
        env->src[0] == '\0' || env->type[0] == '\0') {
        return false;
    }
    if (!have_data) {
        env->data = "{}";
        env->data_len = 2;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* Topics                                                                     */
/* ------------------------------------------------------------------------- */

/** Build "{prefix}/{suffix}", e.g. "g/entrance/event". */
bool cocis_proto_topic_join(char *buf, size_t buf_len,
                            const char *prefix, const char *suffix)
{
    if (buf == NULL || buf_len == 0 || prefix == NULL || suffix == NULL) {
        return false;
    }
    int written = snprintf(buf, buf_len, "%s/%s", prefix, suffix);
    return written > 0 && (size_t)written < buf_len;
}

/** Build "{prefix}/room/{room}/{leaf}", e.g. "g/room/1/telemetry". */
bool cocis_proto_topic_room(char *buf, size_t buf_len,
                            const char *prefix, int room, const char *leaf)
{
    if (buf == NULL || buf_len == 0 || prefix == NULL ||
        leaf == NULL || room <= 0) {
        return false;
    }
    int written = snprintf(buf, buf_len, "%s/room/%d/%s", prefix, room, leaf);
    return written > 0 && (size_t)written < buf_len;
}

/** Build "{prefix}/node/{node}/{leaf}", e.g. "g/node/R2/status". */
bool cocis_proto_topic_node(char *buf, size_t buf_len,
                            const char *prefix, const char *node, const char *leaf)
{
    if (buf == NULL || buf_len == 0 || prefix == NULL ||
        node == NULL || node[0] == '\0' || leaf == NULL) {
        return false;
    }
    int written = snprintf(buf, buf_len, "%s/node/%s/%s", prefix, node, leaf);
    return written > 0 && (size_t)written < buf_len;
}

/* ------------------------------------------------------------------------- */
/* Duplicate suppression                                                      */
/* ------------------------------------------------------------------------- */

/** Empty the duplicate cache (called at boot before subscribing). */
void cocis_dedupe_init(cocis_dedupe_t *cache)
{
    if (cache == NULL) {
        return;
    }
    memset(cache, 0, sizeof(*cache));
}

/**
 * Idempotency check for QoS 1 delivery.
 *
 * A fixed ring buffer is enough: at most a handful of duplicates arrive close
 * together, and MQTT never replays arbitrarily old messages after reconnect.
 * Invalid ids are treated as duplicates so they are dropped early.
 */
bool cocis_dedupe_seen(cocis_dedupe_t *cache, const char *id)
{
    if (cache == NULL) {
        return false;
    }
    if (id == NULL || id[0] == '\0' || strlen(id) >= COCIS_ENVELOPE_ID_MAX) {
        /* Cannot be a valid id, so it can never be a legitimate new message. */
        return true;
    }

    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->ids[i], id) == 0) {
            return true;
        }
    }

    snprintf(cache->ids[cache->next], sizeof(cache->ids[0]), "%s", id);
    cache->next = (cache->next + 1) % COCIS_DEDUPE_SIZE;
    if (cache->count < COCIS_DEDUPE_SIZE) {
        cache->count++;
    }
    return false;
}
