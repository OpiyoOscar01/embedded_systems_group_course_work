/**
 * @file test_scan.c
 * @brief Host tests for the duplicate-scan debounce.
 * @author Opiyo Oscar
 * @requirements FR-ACC-06
 */

#include "scan_filter.h"
#include "test_util.h"

void test_scan_debounce(void)
{
    cocis_scan_filter_t filter;
    cocis_scan_filter_init(&filter);

    CHECK(cocis_scan_filter_accept(&filter, "A1", 0));
    CHECK(!cocis_scan_filter_accept(&filter, "A1", 1999));

    /* The window is half-open: exactly 2 s is accepted. */
    CHECK(cocis_scan_filter_accept(&filter, "A1", 2000));

    /* A different tag is never delayed. */
    CHECK(cocis_scan_filter_accept(&filter, "B2", 2001));
    CHECK(cocis_scan_filter_accept(&filter, "A1", 2002));

    CHECK(!cocis_scan_filter_accept(&filter, "A1", 4001));
    CHECK(cocis_scan_filter_accept(&filter, "A1", 4002));

    CHECK(!cocis_scan_filter_accept(&filter, NULL, 0));
    CHECK(!cocis_scan_filter_accept(&filter, "", 0));
    CHECK(!cocis_scan_filter_accept(NULL, "A1", 0));
}

void test_scan_debounce_wrap(void)
{
    cocis_scan_filter_t filter;
    cocis_scan_filter_init(&filter);

    CHECK(cocis_scan_filter_accept(&filter, "A1", 0xFFFFFF00u));
    CHECK(!cocis_scan_filter_accept(&filter, "A1", 0x00000100u));  /* 512 ms */
    CHECK(!cocis_scan_filter_accept(&filter, "A1", 0x000006CFu));  /* 1999 ms */
    CHECK(cocis_scan_filter_accept(&filter, "A1", 0x000006D0u));   /* 2000 ms */
}
