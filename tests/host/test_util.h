/**
 * @file test_util.h
 * @brief Minimal host-test assertions (no external framework).
 * @author Opiyo Oscar
 * @requirements NFR-07
 */

#pragma once

#include <stdio.h>

extern int g_failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
