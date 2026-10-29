/**
 * @file time_sntp.c
 * @brief ESP-IDF SNTP start-up and East Africa Time conversion.
 * @author Opiyo Oscar
 * @requirements FR-SCH-07, FR-SYN-03
 */

#include "cocis_time_sntp.h"

#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "cocis_time";

esp_err_t cocis_time_sntp_start(void)
{
    setenv("TZ", COCIS_TIME_TZ, 1);
    tzset();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP polling started (TZ=%s)", COCIS_TIME_TZ);
    return ESP_OK;
}

bool cocis_time_now_eat(int *year, int *month, int *day,
                        int *hour, int *minute, int *second)
{
    time_t now = time(NULL);
    struct tm local = {0};
    localtime_r(&now, &local);

    if (local.tm_year + 1900 < COCIS_TIME_MIN_VALID_YEAR) {
        return false;
    }

    if (year != NULL) {
        *year = local.tm_year + 1900;
    }
    if (month != NULL) {
        *month = local.tm_mon + 1;
    }
    if (day != NULL) {
        *day = local.tm_mday;
    }
    if (hour != NULL) {
        *hour = local.tm_hour;
    }
    if (minute != NULL) {
        *minute = local.tm_min;
    }
    if (second != NULL) {
        *second = local.tm_sec;
    }
    return true;
}
