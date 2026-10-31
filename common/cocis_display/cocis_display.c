/**
 * @file cocis_display.c
 * @brief Minimal SSD1306 128x64 I2C text display shared by the nodes.
 * @author Opiyo Oscar
 */

#include "cocis_display.h"

#include <ctype.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "display";

#define DISPLAY_I2C_PORT    I2C_NUM_0
#define DISPLAY_I2C_ADDRESS 0x3C
#define DISPLAY_WIDTH       128
#define DISPLAY_PAGES       8
#define DISPLAY_FB_SIZE     (DISPLAY_WIDTH * DISPLAY_PAGES)

typedef struct {
    char c;
    uint8_t rows[7];
} display_glyph_t;

static const display_glyph_t FONT[] = {
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

static uint8_t s_fb[DISPLAY_FB_SIZE];
static bool s_ready = false;

static esp_err_t display_command(uint8_t command)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (DISPLAY_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x00, true);
    i2c_master_write_byte(link, command, true);
    i2c_master_stop(link);
    esp_err_t err = i2c_master_cmd_begin(DISPLAY_I2C_PORT, link, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(link);
    return err;
}

static esp_err_t display_flush(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t window[] = { 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07 };
    for (size_t i = 0; i < sizeof(window); i++) {
        esp_err_t err = display_command(window[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (DISPLAY_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x40, true);
    i2c_master_write(link, s_fb, sizeof(s_fb), true);
    i2c_master_stop(link);
    esp_err_t err = i2c_master_cmd_begin(DISPLAY_I2C_PORT, link, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(link);
    return err;
}

static const uint8_t *display_glyph(char c)
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

static void display_draw_line(int page, const char *text)
{
    uint8_t *row = &s_fb[page * DISPLAY_WIDTH];
    int x = 0;

    for (const char *p = text; *p != '\0' && x + 6 <= DISPLAY_WIDTH; p++) {
        const uint8_t *glyph = display_glyph(*p);
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

esp_err_t cocis_display_init(int sda_gpio, int scl_gpio)
{
    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };

    esp_err_t err = i2c_param_config(DISPLAY_I2C_PORT, &config);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_driver_install(DISPLAY_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        return err;
    }

    static const uint8_t init[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0xA1, 0x8D, 0x14, 0xAF,
    };
    for (size_t i = 0; i < sizeof(init); i++) {
        err = display_command(init[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SSD1306 not found");
            return err;
        }
    }

    memset(s_fb, 0, sizeof(s_fb));
    s_ready = true;
    display_flush();
    return ESP_OK;
}

bool cocis_display_ready(void)
{
    return s_ready;
}

void cocis_display_show(const char *line1, const char *line2,
                        const char *line3, const char *line4)
{
    if (!s_ready) {
        return;
    }

    memset(s_fb, 0, sizeof(s_fb));
    display_draw_line(0, line1 != NULL ? line1 : "");
    display_draw_line(1, line2 != NULL ? line2 : "");
    display_draw_line(2, line3 != NULL ? line3 : "");
    display_draw_line(3, line4 != NULL ? line4 : "");
    display_flush();
}
