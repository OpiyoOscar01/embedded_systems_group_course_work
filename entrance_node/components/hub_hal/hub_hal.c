/**
 * @file hub_hal.c
 * @brief ESP-IDF implementation of the entrance hub hardware layer.
 * @author Opiyo Oscar
 * @requirements NFR-10, FR-ACC-03, FR-EMG-01, FR-ALR-03
 */

#include "hub_hal.h"

#include <ctype.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "hub_hal";

/* 50 Hz servo period is 20 ms. At 16-bit resolution one period is 65536
 * counts, so 1 ms = 3277 counts and 2 ms = 6554 counts. */
#define SERVO_PERIOD_COUNTS 65536u
#define SERVO_LOCKED_DUTY   (SERVO_PERIOD_COUNTS / 20u)
#define SERVO_OPEN_DUTY     (SERVO_PERIOD_COUNTS / 10u)

#define BUZZER_TIMER        LEDC_TIMER_1
#define BUZZER_CHANNEL      LEDC_CHANNEL_2
#define BUZZER_DUTY         512u   /* ~50% of the 10-bit range */

/* The ISR only latches a flag. Collapsing a burst of bounce edges into one
 * flag is enough debounce for a human press and keeps the ISR minimal. */
static volatile bool s_emergency_pressed = false;
static volatile bool s_tamper_event = false;

static esp_timer_handle_t s_beep_timer = NULL;
static hub_diag_t s_diag = {0};

/* ------------------------------------------------------------------------- */
/* Buzzer timer                                                               */
/* ------------------------------------------------------------------------- */

/** One-shot timer callback that silences the buzzer. */
static void beep_stop_callback(void *arg)
{
    (void)arg;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
}

/* ------------------------------------------------------------------------- */
/* Button ISRs                                                                */
/* ------------------------------------------------------------------------- */

/** Emergency button: falling edge, latch a single pending event. */
static void IRAM_ATTR emergency_isr(void *arg)
{
    (void)arg;
    s_emergency_pressed = true;
}

/** Tamper switch: falling edge, latch a single pending event. */
static void IRAM_ATTR tamper_isr(void *arg)
{
    (void)arg;
    s_tamper_event = true;
}

/* ------------------------------------------------------------------------- */
/* Initialisation helpers                                                     */
/* ------------------------------------------------------------------------- */

/** Configure a plain push-pull output. */
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

/** Configure an input-only pin with an external pull-up and falling ISR. */
static void configure_input_isr(gpio_num_t pin, gpio_isr_t handler)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin, handler, NULL));
}

/** Configure a LEDC channel for a servo. */
static esp_err_t configure_servo(gpio_num_t pin, ledc_channel_t channel)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_16_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t config = {
        .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = SERVO_LOCKED_DUTY,
        .hpoint = 0,
    };
    return ledc_channel_config(&config);
}

/** Configure the buzzer LEDC channel. */
static esp_err_t configure_buzzer(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BUZZER_TIMER,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t config = {
        .gpio_num = HUB_PIN_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BUZZER_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&config);
}

/* ------------------------------------------------------------------------- */
/* Optional SSD1306 status display (I2C, address 0x3C)                        */
/* ------------------------------------------------------------------------- */

#define OLED_I2C_PORT    I2C_NUM_0
#define OLED_I2C_ADDRESS 0x3C
#define OLED_WIDTH       128
#define OLED_PAGES       8
#define OLED_FB_SIZE     (OLED_WIDTH * OLED_PAGES)
#define OLED_LINE_CHARS  21

typedef struct {
    char c;
    uint8_t rows[7];   /* 5-bit rows, bit 4 is the leftmost pixel */
} oled_glyph_t;

static const oled_glyph_t FONT[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { ':', { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '/', { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06 } },
    { '%', { 0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13 } },
};

static uint8_t s_oled_fb[OLED_FB_SIZE];
static bool s_oled_ready = false;

/** Send one command byte to the panel. */
static esp_err_t oled_command(uint8_t command)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x00, true);
    i2c_master_write_byte(link, command, true);
    i2c_master_stop(link);
    esp_err_t err = i2c_master_cmd_begin(OLED_I2C_PORT, link, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(link);
    return err;
}

/** Push the whole framebuffer to the panel. */
static esp_err_t oled_flush(void)
{
    if (!s_oled_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Horizontal addressing, full window. */
    const uint8_t window[] = {
        0x21, 0x00, 0x7F,
        0x22, 0x00, 0x07,
    };
    for (size_t i = 0; i < sizeof(window); i++) {
        esp_err_t err = oled_command(window[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x40, true);
    i2c_master_write(link, s_oled_fb, sizeof(s_oled_fb), true);
    i2c_master_stop(link);
    esp_err_t err = i2c_master_cmd_begin(OLED_I2C_PORT, link, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(link);
    return err;
}

/** Fetch a glyph; lowercase is folded to uppercase, unknown -> '?'. */
static const uint8_t *oled_glyph(char c)
{
    static const uint8_t unknown[7] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
    c = (char)toupper((unsigned char)c);
    for (size_t i = 0; i < sizeof(FONT) / sizeof(FONT[0]); i++) {
        if (FONT[i].c == c) {
            return FONT[i].rows;
        }
    }
    return unknown;
}

/** Draw one text line into a page of the framebuffer. */
static void oled_draw_line(int page, const char *text)
{
    uint8_t *row = &s_oled_fb[page * OLED_WIDTH];
    int x = 0;

    for (const char *p = text; *p != '\0' && x + 6 <= OLED_WIDTH; p++) {
        const uint8_t *glyph = oled_glyph(*p);
        for (int col = 0; col < 5; col++) {
            uint8_t byte = 0;
            for (int bit = 0; bit < 7; bit++) {
                if (glyph[bit] & (1u << (4 - col))) {
                    byte |= (uint8_t)(1u << bit);
                }
            }
            row[x++] = byte;
        }
        row[x++] = 0;
    }
}

/** Probe and configure the optional SSD1306 panel. */
static esp_err_t oled_init(void)
{
    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = HUB_PIN_I2C_SDA,
        .scl_io_num = HUB_PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };

    esp_err_t err = i2c_param_config(OLED_I2C_PORT, &config);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_driver_install(OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        return err;
    }

    static const uint8_t init[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0xA1, 0x8D, 0x14, 0xAF,
    };
    for (size_t i = 0; i < sizeof(init); i++) {
        err = oled_command(init[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    memset(s_oled_fb, 0, sizeof(s_oled_fb));
    s_oled_ready = true;
    oled_flush();
    return ESP_OK;
}

bool hub_hal_display_ready(void)
{
    return s_oled_ready;
}

void hub_hal_display_show(const char *line1, const char *line2,
                          const char *line3, const char *line4)
{
    if (!s_oled_ready) {
        return;
    }

    memset(s_oled_fb, 0, sizeof(s_oled_fb));
    oled_draw_line(0, line1 != NULL ? line1 : "");
    oled_draw_line(1, line2 != NULL ? line2 : "");
    oled_draw_line(2, line3 != NULL ? line3 : "");
    oled_draw_line(3, line4 != NULL ? line4 : "");
    oled_flush();
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

esp_err_t hub_hal_init(void)
{
    s_diag = (hub_diag_t){0};

    /* LEDs first so a failure is visible as early as possible. */
    configure_output(HUB_PIN_LED_ENTRY_GREEN);
    configure_output(HUB_PIN_LED_ENTRY_RED);
    configure_output(HUB_PIN_LED_EXIT_GREEN);
    configure_output(HUB_PIN_LED_EXIT_RED);
    s_diag.gpio_ok = true;

    /* Servos. */
    esp_err_t err = configure_servo(HUB_PIN_SERVO_ENTRY, LEDC_CHANNEL_0);
    if (err == ESP_OK) {
        err = configure_servo(HUB_PIN_SERVO_EXIT, LEDC_CHANNEL_1);
    }
    s_diag.servo_ok = (err == ESP_OK);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "servo init failed: %s", esp_err_to_name(err));
    }

    /* Buzzer + one-shot stop timer. */
    err = configure_buzzer();
    if (err == ESP_OK && s_beep_timer == NULL) {
        esp_timer_create_args_t args = {
            .callback = beep_stop_callback,
            .name = "hub_beep",
        };
        err = esp_timer_create(&args, &s_beep_timer);
    }
    s_diag.buzzer_ok = (err == ESP_OK);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "buzzer init failed: %s", esp_err_to_name(err));
    }

    /* Buttons: the ISR service may already be installed by another part of
     * the firmware, so ESP_ERR_INVALID_STATE is treated as success. */
    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service failed: %s", esp_err_to_name(isr_err));
        s_diag.buttons_ok = false;
    } else {
        configure_input_isr(HUB_PIN_BUTTON_EMERGENCY, emergency_isr);
        configure_input_isr(HUB_PIN_BUTTON_TAMPER, tamper_isr);

        gpio_config_t passage = {
            .pin_bit_mask = 1ULL << HUB_PIN_PASSAGE,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&passage));
        s_diag.buttons_ok = true;
    }

    /* Optional I2C status panel; the hub still works without it. */
    err = oled_init();
    s_diag.oled_ok = (err == ESP_OK);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OLED not available: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "hub HAL ready (servo=%d buzzer=%d buttons=%d oled=%d)",
             s_diag.servo_ok, s_diag.buzzer_ok, s_diag.buttons_ok,
             s_diag.oled_ok);
    return ESP_OK;
}

void hub_hal_show_result(hub_door_t door, bool granted)
{
    gpio_num_t green = (door == HUB_DOOR_ENTRY) ? HUB_PIN_LED_ENTRY_GREEN
                                                : HUB_PIN_LED_EXIT_GREEN;
    gpio_num_t red = (door == HUB_DOOR_ENTRY) ? HUB_PIN_LED_ENTRY_RED
                                              : HUB_PIN_LED_EXIT_RED;

    gpio_set_level(green, granted ? 1 : 0);
    gpio_set_level(red, granted ? 0 : 1);
}

void hub_hal_leds_off(hub_door_t door)
{
    gpio_num_t green = (door == HUB_DOOR_ENTRY) ? HUB_PIN_LED_ENTRY_GREEN
                                                : HUB_PIN_LED_EXIT_GREEN;
    gpio_num_t red = (door == HUB_DOOR_ENTRY) ? HUB_PIN_LED_ENTRY_RED
                                              : HUB_PIN_LED_EXIT_RED;
    gpio_set_level(green, 0);
    gpio_set_level(red, 0);
}

void hub_hal_set_door(hub_door_t door, bool open)
{
    ledc_channel_t channel = (door == HUB_DOOR_ENTRY) ? LEDC_CHANNEL_0
                                                      : LEDC_CHANNEL_1;
    uint32_t duty = open ? SERVO_OPEN_DUTY : SERVO_LOCKED_DUTY;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

void hub_hal_beep(uint32_t duration_ms)
{
    if (s_beep_timer == NULL || duration_ms == 0) {
        return;
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, BUZZER_DUTY);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);

    esp_timer_stop(s_beep_timer);
    esp_timer_start_once(s_beep_timer, (uint64_t)duration_ms * 1000u);
}

bool hub_hal_passage_detected(void)
{
    return gpio_get_level(HUB_PIN_PASSAGE) != 0;
}

bool hub_hal_take_emergency_press(void)
{
    if (!s_emergency_pressed) {
        return false;
    }
    s_emergency_pressed = false;
    return true;
}

bool hub_hal_take_tamper_event(void)
{
    if (!s_tamper_event) {
        return false;
    }
    s_tamper_event = false;
    return true;
}

hub_diag_t hub_hal_diag(void)
{
    return s_diag;
}
