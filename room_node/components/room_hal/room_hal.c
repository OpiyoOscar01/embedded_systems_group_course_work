/**
 * @file room_hal.c
 * @brief ESP-IDF implementation of the room hardware layer.
 * @author Opiyo Oscar
 * @requirements NFR-10, FR-LGT-01, FR-LGT-05, FR-LGT-06
 */

#include "room_hal.h"

#include "cocis_display.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "room_hal";

static adc_oneshot_unit_handle_t s_adc = NULL;
static room_diag_t s_diag = {0};
static volatile bool s_override_pressed = false;

/* The LDR module output swings over the full ADC range in the simulator. */
#define LDR_RAW_MAX 4095
#define LDR_LUX_MAX 1000

/** Manual override button ISR: latch one pending press. */
static void IRAM_ATTR override_isr(void *arg)
{
    (void)arg;
    s_override_pressed = true;
}

/** Configure a push-pull output and drive it low. */
static void configure_output(gpio_num_t pin)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    gpio_set_level(pin, 0);
}

/** Configure a plain digital input. */
static void configure_input(gpio_num_t pin)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
}

esp_err_t room_hal_init(void)
{
    s_diag = (room_diag_t){0};

    configure_output(ROOM_PIN_RELAY);
    configure_output(ROOM_PIN_LED_OCCUPIED);
    configure_output(ROOM_PIN_LED_LIGHTS);
    configure_output(ROOM_PIN_LED_NETWORK);
    s_diag.gpio_ok = true;

    configure_input(ROOM_PIN_PIR);

    /* Override button: external pull-up, active low, falling edge. */
    gpio_config_t button = {
        .pin_bit_mask = 1ULL << ROOM_PIN_OVERRIDE_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));

    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err == ESP_OK || isr_err == ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(ROOM_PIN_OVERRIDE_BUTTON,
                                             override_isr, NULL));
        s_diag.button_ok = true;
    }

    /* ADC1 oneshot for the LDR. 12 dB attenuation covers the 3.3 V module. */
    adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit, &s_adc);
    if (err == ESP_OK) {
        adc_oneshot_chan_cfg_t channel = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        err = adc_oneshot_config_channel(s_adc, ROOM_LDR_ADC_CHANNEL, &channel);
    }
    s_diag.adc_ok = (err == ESP_OK);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LDR ADC init failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "room HAL ready (adc=%d button=%d)", s_diag.adc_ok, s_diag.button_ok);

    if (cocis_display_init(ROOM_PIN_I2C_SDA, ROOM_PIN_I2C_SCL) != ESP_OK) {
        ESP_LOGW(TAG, "status display not available");
    }

    return ESP_OK;
}

bool room_hal_read_motion(void)
{
    return gpio_get_level(ROOM_PIN_PIR) != 0;
}

int room_hal_read_lux(void)
{
    if (s_adc == NULL) {
        return 0;
    }

    int raw = 0;
    if (adc_oneshot_read(s_adc, ROOM_LDR_ADC_CHANNEL, &raw) != ESP_OK) {
        return 0;
    }

    /* Linear approximation; the thresholds are tuned in settings. */
    if (raw < 0) {
        raw = 0;
    }
    if (raw > LDR_RAW_MAX) {
        raw = LDR_RAW_MAX;
    }
    return (raw * LDR_LUX_MAX) / LDR_RAW_MAX;
}

void room_hal_set_relay(bool on)
{
    gpio_set_level(ROOM_PIN_RELAY, on ? 1 : 0);
}

void room_hal_set_led_occupied(bool on)
{
    gpio_set_level(ROOM_PIN_LED_OCCUPIED, on ? 1 : 0);
}

void room_hal_set_led_lights(bool on)
{
    gpio_set_level(ROOM_PIN_LED_LIGHTS, on ? 1 : 0);
}

void room_hal_set_led_network(bool on)
{
    gpio_set_level(ROOM_PIN_LED_NETWORK, on ? 1 : 0);
}

bool room_hal_take_override_press(void)
{
    if (!s_override_pressed) {
        return false;
    }
    s_override_pressed = false;
    return true;
}

room_diag_t room_hal_diag(void)
{
    return s_diag;
}

bool room_hal_display_ready(void)
{
    return cocis_display_ready();
}

void room_hal_display_show(const char *line1, const char *line2,
                           const char *line3, const char *line4)
{
    cocis_display_show(line1, line2, line3, line4);
}
