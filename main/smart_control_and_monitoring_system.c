#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "LCD";

#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_SDA_IO           21
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100000
#define I2C_MASTER_TX_BUF_DISABLE   0
#define I2C_MASTER_RX_BUF_DISABLE   0
#define I2C_MASTER_TIMEOUT_MS       1000

#define LCD_ADDR                    0x27
#define LCD_BACKLIGHT               0x08
#define LCD_ENABLE                  0x04
#define LCD_RS                      0x01

static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode,
                              I2C_MASTER_RX_BUF_DISABLE,
                              I2C_MASTER_TX_BUF_DISABLE, 0);
}

static void lcd_write_byte(uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LCD_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, data | LCD_BACKLIGHT, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
}

static void lcd_send_nibble(uint8_t nibble, uint8_t mode)
{
    uint8_t data = (nibble & 0xF0) | mode;
    lcd_write_byte(data);
    lcd_write_byte(data | LCD_ENABLE);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write_byte(data & ~LCD_ENABLE);
    vTaskDelay(pdMS_TO_TICKS(1));
}

static void lcd_send_byte(uint8_t value, uint8_t mode)
{
    lcd_send_nibble(value & 0xF0, mode);
    lcd_send_nibble((value << 4) & 0xF0, mode);
}

static void lcd_cmd(uint8_t cmd)
{
    lcd_send_byte(cmd, 0);
}

static void lcd_data(uint8_t data)
{
    lcd_send_byte(data, LCD_RS);
}

static void lcd_init(void)
{
    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_send_nibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_send_nibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_send_nibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_send_nibble(0x20, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    lcd_cmd(0x28);
    lcd_cmd(0x0C);
    lcd_cmd(0x06);
    lcd_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void lcd_print(const char *str)
{
    while (*str) {
        lcd_data((uint8_t)*str++);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing I2C and LCD...");
    ESP_ERROR_CHECK(i2c_master_init());
    lcd_init();
    lcd_cmd(0x80);
    lcd_print("Welcome to Embedded Coursework.");
    ESP_LOGI(TAG, "Displayed on LCD");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
