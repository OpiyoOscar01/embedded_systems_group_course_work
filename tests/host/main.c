/**
 * @file main.c
 * @brief Host test runner.
 * @author Opiyo Oscar
 * @requirements NFR-07
 */

#include <stdio.h>

#include "test_util.h"

int g_failures = 0;

void test_types(void);
void test_log_chain(void);
void test_log_chain_limits(void);

int main(void)
{
    printf("Running host tests...\n");

    test_types();
    test_log_chain();
    test_log_chain_limits();

    if (g_failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }

    printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
