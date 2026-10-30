/**
 * @file test_log_chain.c
 * @brief Host tests for the hash-chain builder and verifier.
 * @author Opiyo Oscar
 * @requirements FR-STO-05, INN-03
 */

#include "cocis_log_chain.h"
#include "test_util.h"

#include <string.h>

/* Deterministic fake hash so the chain logic can be tested without mbedtls. */
static void fake_hash(const uint8_t *data, size_t len, uint8_t out[COCIS_HASH_LEN])
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    for (size_t i = 0; i < COCIS_HASH_LEN; i++) {
        out[i] = (uint8_t)(h >> ((i % 4) * 8)) ^ (uint8_t)(i * 31u + len);
    }
}

static void build_chain(const char *const *texts, size_t count,
                        const uint8_t genesis[COCIS_HASH_LEN],
                        cocis_log_record_t *records)
{
    uint8_t prev[COCIS_HASH_LEN];
    memcpy(prev, genesis, COCIS_HASH_LEN);
    for (size_t i = 0; i < count; i++) {
        records[i].text = texts[i];
        bool ok = cocis_chain_next(fake_hash, prev, texts[i], records[i].stored_hash);
        CHECK(ok);
        memcpy(prev, records[i].stored_hash, COCIS_HASH_LEN);
    }
}

void test_log_chain(void)
{
    uint8_t genesis[COCIS_HASH_LEN] = {0};
    const char *texts[3] = {"row-1", "row-2", "row-3"};
    cocis_log_record_t records[3];

    build_chain(texts, 3, genesis, records);

    CHECK_EQ(cocis_chain_verify(fake_hash, genesis, records, 3), -1);

    /* Tampering with a record text breaks the chain at that index. */
    records[1].text = "row-2-edited";
    CHECK_EQ(cocis_chain_verify(fake_hash, genesis, records, 3), 1);

    /* Restore and instead tamper with the stored hash. */
    records[1].text = "row-2";
    records[2].stored_hash[0] ^= 0xFF;
    CHECK_EQ(cocis_chain_verify(fake_hash, genesis, records, 3), 2);
}

void test_log_chain_hex(void)
{
    const uint8_t bytes[4] = {0x00, 0x0F, 0xA5, 0xFF};
    char hex[9];
    cocis_hex_encode(bytes, sizeof(bytes), hex);
    CHECK(strcmp(hex, "000fa5ff") == 0);

    uint8_t decoded[4];
    CHECK(cocis_hex_decode(hex, decoded, sizeof(decoded)));
    CHECK(memcmp(decoded, bytes, sizeof(bytes)) == 0);

    /* Uppercase input is accepted; wrong length or bad digits are not. */
    CHECK(cocis_hex_decode("000FA5FF", decoded, sizeof(decoded)));
    CHECK(!cocis_hex_decode("000", decoded, sizeof(decoded)));
    CHECK(!cocis_hex_decode("0000000g", decoded, sizeof(decoded)));
    CHECK(!cocis_hex_decode(NULL, decoded, sizeof(decoded)));
}

void test_log_chain_limits(void)
{
    uint8_t prev[COCIS_HASH_LEN] = {0};
    uint8_t out[COCIS_HASH_LEN];
    char big[COCIS_LOG_RECORD_MAX + 2];
    memset(big, 'x', sizeof(big));
    big[sizeof(big) - 1] = '\0';

    CHECK(cocis_chain_next(fake_hash, prev, big, out) == false);

    /* Empty chain is valid. */
    CHECK_EQ(cocis_chain_verify(fake_hash, prev, NULL, 0), -1);
}
