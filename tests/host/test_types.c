/**
 * @file test_types.c
 * @brief Host checks for shared types and constants.
 * @author Opiyo Oscar
 * @requirements FR-ACC-05
 */

#include "cocis_types.h"
#include "test_util.h"

#include <string.h>

void test_types(void)
{
    CHECK_EQ(COCIS_DIR_ENTRY, 0);
    CHECK_EQ(COCIS_DIR_EXIT, 1);
    CHECK_EQ(COCIS_RES_GRANTED, 0);
    CHECK_EQ(COCIS_RES_COUNT, 12);

    CHECK(strcmp(cocis_result_str(COCIS_RES_DENY_ANTIPASSBACK), "DENY_ANTIPASSBACK") == 0);
    CHECK(strcmp(cocis_result_str(COCIS_RES_ANOMALY_EXIT_NO_ENTRY), "ANOMALY_EXIT_NO_ENTRY") == 0);
    CHECK(strcmp(cocis_result_str(COCIS_RES_COUNT), "UNKNOWN") == 0);

    CHECK_EQ(COCIS_DOOR_OPEN_MS, 5000);
    CHECK_EQ(COCIS_DUP_SCAN_MS, 2000);
    CHECK_EQ(COCIS_T_VAC_S, 300);
    CHECK_EQ(COCIS_T_WARN_S, 30);
    CHECK_EQ(COCIS_LUX_ON, 600);
    CHECK_EQ(COCIS_LUX_OFF, 800);
}
