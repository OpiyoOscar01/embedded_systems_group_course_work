/**
 * @file test_proto.c
 * @brief Host tests for the envelope codec, topic builders and dedupe cache.
 *
 * These tests exercise the exact code that runs on the ESP32, so they act as
 * a fast regression net for the wire protocol.
 *
 * @author Opiyo Oscar
 * @requirements FR-SYN-03, FR-DST-02
 */

#include "cocis_proto.h"
#include "test_util.h"

#include <string.h>

/** Fill an envelope with the example values from the protocol specification. */
static void fill_example(cocis_envelope_t *env)
{
    memset(env, 0, sizeof(*env));
    env->version = COCIS_PROTO_VERSION;
    strcpy(env->id, "R1-0007-000142");
    env->ts = 1793088000ULL;
    strcpy(env->src, "R1");
    strcpy(env->type, "tele");
}

void test_proto_event_ids(void)
{
    char id[COCIS_ENVELOPE_ID_MAX];

    CHECK(cocis_proto_make_id(id, sizeof(id), "R1", 7, 142));
    CHECK(strcmp(id, "R1-0007-000142") == 0);

    /* Small buffers and empty sources are rejected. */
    CHECK(!cocis_proto_make_id(id, 8, "R1", 7, 142));
    CHECK(!cocis_proto_make_id(id, sizeof(id), "", 7, 142));
    CHECK(!cocis_proto_make_id(NULL, sizeof(id), "R1", 7, 142));

    cocis_event_id_t gen;
    cocis_event_id_init(&gen, "R2", 12);
    CHECK(cocis_event_id_next(&gen, id, sizeof(id)));
    CHECK(strcmp(id, "R2-0012-000000") == 0);
    CHECK(cocis_event_id_next(&gen, id, sizeof(id)));
    CHECK(strcmp(id, "R2-0012-000001") == 0);
    CHECK(gen.seq == 2);
}

void test_proto_encode_decode(void)
{
    cocis_envelope_t env;
    fill_example(&env);

    char json[256];
    int len = cocis_proto_encode(json, sizeof(json), &env, "{\"occ\":true,\"lux\":412}");
    CHECK(len > 0);
    CHECK(strncmp(json, "{\"v\":1,\"id\":\"R1-0007-000142\",\"ts\":1793088000,", 45) == 0);
    CHECK(strstr(json, "\"src\":\"R1\"") != NULL);
    CHECK(strstr(json, "\"type\":\"tele\"") != NULL);
    CHECK(strstr(json, "\"data\":{\"occ\":true,\"lux\":412}") != NULL);

    cocis_envelope_t decoded;
    CHECK(cocis_proto_decode(json, &decoded));
    CHECK_EQ(decoded.version, 1);
    CHECK(strcmp(decoded.id, "R1-0007-000142") == 0);
    CHECK(decoded.ts == 1793088000ULL);
    CHECK(strcmp(decoded.src, "R1") == 0);
    CHECK(strcmp(decoded.type, "tele") == 0);

    const char *expected_data = "{\"occ\":true,\"lux\":412}";
    CHECK_EQ(decoded.data_len, strlen(expected_data));
    CHECK(strncmp(decoded.data, expected_data, decoded.data_len) == 0);

    /* NULL payload encodes as an empty object. */
    len = cocis_proto_encode(json, sizeof(json), &env, NULL);
    CHECK(len > 0);
    CHECK(strstr(json, "\"data\":{}") != NULL);
    CHECK(cocis_proto_decode(json, &decoded));
    CHECK_EQ(decoded.data_len, 2);
    CHECK(strncmp(decoded.data, "{}", 2) == 0);

    /* Too small a buffer must fail rather than truncate silently. */
    CHECK_EQ(cocis_proto_encode(json, 16, &env, NULL), -1);
}

void test_proto_escaping(void)
{
    cocis_envelope_t env;
    fill_example(&env);
    strcpy(env.type, "a\"b");

    char json[256];
    CHECK(cocis_proto_encode(json, sizeof(json), &env, "{}") > 0);
    CHECK(strstr(json, "\"type\":\"a\\\"b\"") != NULL);

    cocis_envelope_t decoded;
    CHECK(cocis_proto_decode(json, &decoded));
    CHECK(strcmp(decoded.type, "a\"b") == 0);
}

void test_proto_decode_edge_cases(void)
{
    cocis_envelope_t env;

    /* Whitespace and unknown fields are tolerated. */
    const char *with_extra =
        " { \"v\" : 1 , \"id\" : \"H-0001-000003\" , \"ts\" : 5 ,"
        " \"src\" : \"H\" , \"extra\" : {\"a\":[1,2]} ,"
        " \"type\" : \"emergency\" , \"data\" : {\"on\":true} } ";
    CHECK(cocis_proto_decode(with_extra, &env));
    CHECK(strcmp(env.id, "H-0001-000003") == 0);
    CHECK(strcmp(env.type, "emergency") == 0);
    CHECK_EQ(env.data_len, strlen("{\"on\":true}"));
    CHECK(strncmp(env.data, "{\"on\":true}", env.data_len) == 0);

    /* Missing required fields fail. */
    CHECK(!cocis_proto_decode("{\"v\":1,\"id\":\"x\",\"src\":\"H\",\"type\":\"t\"}", &env));
    CHECK(!cocis_proto_decode("{\"v\":1,\"id\":\"x\",\"ts\":1,\"src\":\"H\"}", &env));

    /* Malformed JSON fails. */
    CHECK(!cocis_proto_decode("{\"v\":1,", &env));
    CHECK(!cocis_proto_decode("{\"v\":1,\"id\":\"x\",\"ts\":1,\"src\":\"H\",\"type\":\"t\"}x", &env));
    CHECK(!cocis_proto_decode("{\"v\":0,\"id\":\"x\",\"ts\":1,\"src\":\"H\",\"type\":\"t\"}", &env));
    CHECK(!cocis_proto_decode("", &env));
    CHECK(!cocis_proto_decode(NULL, &env));
}

void test_proto_topics(void)
{
    char topic[COCIS_TOPIC_MAX];

    CHECK(cocis_proto_topic_join(topic, sizeof(topic), "g", "entrance/event"));
    CHECK(strcmp(topic, "g/entrance/event") == 0);

    CHECK(cocis_proto_topic_room(topic, sizeof(topic), "g", 1, "telemetry"));
    CHECK(strcmp(topic, "g/room/1/telemetry") == 0);

    CHECK(cocis_proto_topic_node(topic, sizeof(topic), "g", "R2", "status"));
    CHECK(strcmp(topic, "g/node/R2/status") == 0);

    /* Invalid arguments and overflow are rejected. */
    CHECK(!cocis_proto_topic_room(topic, sizeof(topic), "g", 0, "telemetry"));
    CHECK(!cocis_proto_topic_node(topic, 8, "g", "R2", "status"));
    CHECK(!cocis_proto_topic_join(topic, sizeof(topic), NULL, "x"));
}

void test_proto_dedupe(void)
{
    cocis_dedupe_t cache;
    cocis_dedupe_init(&cache);

    CHECK(!cocis_dedupe_seen(&cache, "R1-0001-000001"));
    CHECK(cocis_dedupe_seen(&cache, "R1-0001-000001"));
    CHECK(!cocis_dedupe_seen(&cache, "R1-0001-000002"));
    CHECK(cocis_dedupe_seen(&cache, "R1-0001-000002"));

    /* Old entries are evicted once the ring is full. */
    char id[COCIS_ENVELOPE_ID_MAX];
    for (int i = 0; i < COCIS_DEDUPE_SIZE; i++) {
        snprintf(id, sizeof(id), "X-0001-%06d", i);
        CHECK(!cocis_dedupe_seen(&cache, id));
    }
    CHECK(!cocis_dedupe_seen(&cache, "R1-0001-000001"));

    /* An id that cannot be valid is always dropped. */
    CHECK(cocis_dedupe_seen(&cache, "this-id-is-way-too-long-to-be-valid"));
    CHECK(cocis_dedupe_seen(&cache, NULL));
}
