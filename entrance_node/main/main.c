/**
 * @file main.c
 * @brief Entrance Hub application entry (smoke test banner + LED check).
 * @author Opiyo Oscar
 * @requirements FR-DST-01
 */

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "ENTRANCE";

#define LED_GREEN GPIO_NUM_2
#define LED_RED   GPIO_NUM_4

void app_main(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GREEN) | (1ULL << LED_RED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Entrance Hub booting (smoke test)");

    while (1) {
        gpio_set_level(LED_GREEN, 1);
        gpio_set_level(LED_RED, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_GREEN, 0);
        gpio_set_level(LED_RED, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
