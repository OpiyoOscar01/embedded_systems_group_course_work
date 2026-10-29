/**
 * @file main.c
 * @brief Room node application entry (smoke test banner + network LED check).
 * @author Opiyo Oscar
 * @requirements FR-DST-01, FR-DST-05
 */

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ROOM";

#define LED_NETWORK GPIO_NUM_2

void app_main(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_NETWORK),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Room node booting, ROOM_ID=%d", CONFIG_COCIS_ROOM_ID);

    while (1) {
        gpio_set_level(LED_NETWORK, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_NETWORK, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
