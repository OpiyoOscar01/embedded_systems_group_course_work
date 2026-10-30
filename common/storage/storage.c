/**
 * @file storage.c
 * @brief SDSPI mount and mutex-protected FATFS helpers.
 * @author Opiyo Oscar
 * @requirements FR-STO-01..04, FR-SYN-01, NFR-10
 */

#include "storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

static const char *TAG = "storage";

/* SPI pin map from Table 20. The RFID readers share this bus in hardware but
 * are simulated through the HAL, so only the SD card is mounted here. */
#define STORAGE_PIN_SCK   18
#define STORAGE_PIN_MISO  19
#define STORAGE_PIN_MOSI  23
#define STORAGE_PIN_CS    5

#define STORAGE_PATH_MAX  128
#define STORAGE_LOCK_MS   1000

static bool s_ready = false;
static SemaphoreHandle_t s_mutex = NULL;
static sdmmc_card_t *s_card = NULL;

/** Build "/sdcard/<relative>" and reject path traversal. */
static bool build_path(const char *relative_path, char *out, size_t out_len)
{
    if (relative_path == NULL || relative_path[0] == '\0' || out == NULL ||
        strstr(relative_path, "..") != NULL) {
        return false;
    }

    int written = snprintf(out, out_len, "%s/%s", STORAGE_MOUNT_POINT, relative_path);
    return written > 0 && (size_t)written < out_len;
}

/** Create one directory, ignoring "already exists". */
static void make_dir(const char *path)
{
    if (mkdir(path, 0777) != 0) {
        ESP_LOGD(TAG, "mkdir %s (already present or failed)", path);
    }
}

/** Create the directory layout described in Table 32. */
static void create_layout(void)
{
    make_dir(STORAGE_MOUNT_POINT "/logs");
    make_dir(STORAGE_MOUNT_POINT "/logs/access");
    make_dir(STORAGE_MOUNT_POINT "/logs/audit");
    make_dir(STORAGE_MOUNT_POINT "/logs/energy");
    make_dir(STORAGE_MOUNT_POINT "/config");
    make_dir(STORAGE_MOUNT_POINT "/state");
    make_dir(STORAGE_MOUNT_POINT "/export");
}

esp_err_t storage_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    /* Mount the card as a FAT volume on the SPI bus. */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus = {
        .mosi_io_num = STORAGE_PIN_MOSI,
        .miso_io_num = STORAGE_PIN_MISO,
        .sclk_io_num = STORAGE_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t err = spi_bus_initialize(host.slot, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI bus init failed: %s (running without SD)",
                 esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = STORAGE_PIN_CS;
    slot.host_id = host.slot;

    err = esp_vfs_fat_sdspi_mount(STORAGE_MOUNT_POINT, &host, &slot,
                                  &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s (running without SD)", esp_err_to_name(err));
        return err;
    }

    create_layout();
    s_ready = true;
    ESP_LOGI(TAG, "SD card mounted at %s", STORAGE_MOUNT_POINT);
    return ESP_OK;
}

bool storage_ready(void)
{
    return s_ready;
}

esp_err_t storage_append(const char *relative_path, const char *line)
{
    if (!s_ready || line == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char path[STORAGE_PATH_MAX];
    if (!build_path(relative_path, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(STORAGE_LOCK_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    FILE *file = fopen(path, "a");
    if (file == NULL) {
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "cannot open %s for append", path);
        return ESP_FAIL;
    }

    size_t length = strlen(line);
    size_t written = fwrite(line, 1, length, file);
    if (written == length && (length == 0 || line[length - 1] != '\n')) {
        fputc('\n', file);
    }

    fflush(file);
    fclose(file);
    xSemaphoreGive(s_mutex);

    return written == length ? ESP_OK : ESP_FAIL;
}

esp_err_t storage_write(const char *relative_path, const char *text)
{
    if (!s_ready || text == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char path[STORAGE_PATH_MAX];
    if (!build_path(relative_path, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(STORAGE_LOCK_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    size_t length = strlen(text);
    size_t written = fwrite(text, 1, length, file);
    fflush(file);
    fclose(file);
    xSemaphoreGive(s_mutex);

    return written == length ? ESP_OK : ESP_FAIL;
}

esp_err_t storage_read(const char *relative_path, char *buffer, size_t buffer_len,
                       size_t *out_len)
{
    if (!s_ready || buffer == NULL || buffer_len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    char path[STORAGE_PATH_MAX];
    if (!build_path(relative_path, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(STORAGE_LOCK_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    size_t total = fread(buffer, 1, buffer_len - 1, file);
    bool too_big = !feof(file);
    fclose(file);
    xSemaphoreGive(s_mutex);

    buffer[total] = '\0';
    if (out_len != NULL) {
        *out_len = total;
    }
    return too_big ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

bool storage_exists(const char *relative_path)
{
    char path[STORAGE_PATH_MAX];
    if (!s_ready || !build_path(relative_path, path, sizeof(path))) {
        return false;
    }

    struct stat info;
    return stat(path, &info) == 0;
}
