/**
 * @file cocis_time_sntp.h
 * @brief ESP-IDF SNTP start-up and East Africa Time clock access.
 * @author Opiyo Oscar
 * @requirements FR-SCH-07, FR-SYN-03
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define COCIS_TIME_MIN_VALID_YEAR 2024
#define COCIS_TIME_TZ             "EAT-3"

/* Configure EAT and start SNTP polling in the background. */
esp_err_t cocis_time_sntp_start(void);

/* Read the current local (EAT) time. Returns false before synchronisation. */
bool cocis_time_now_eat(int *year, int *month, int *day,
                        int *hour, int *minute, int *second);
