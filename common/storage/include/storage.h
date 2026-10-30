/**
 * @file storage.h
 * @brief SD card mounting and serialised file access.
 *
 * The SD card holds logs, configuration and state. All access goes through
 * this component so there is a single writer and a single mutex around the
 * shared SPI bus and FATFS (FR-SYN-01). If the card cannot be mounted the
 * system keeps running and reports a degraded state; callers receive
 * ESP_ERR_INVALID_STATE from the file helpers.
 *
 * Paths passed to the API are relative to the mount point, for example
 * "logs/access/20261029.csv".
 *
 * @author Opiyo Oscar
 * @requirements FR-STO-01..04, FR-SYN-01, NFR-10
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define STORAGE_MOUNT_POINT "/sdcard"

/** Mount the card and create the directory layout. */
esp_err_t storage_init(void);

/** True once the card is mounted and usable. */
bool storage_ready(void);

/** Append one line (a newline is added when missing). */
esp_err_t storage_append(const char *relative_path, const char *line);

/** Create or overwrite a text file. */
esp_err_t storage_write(const char *relative_path, const char *text);

/**
 * Read a whole text file into `buffer` (NUL terminated).
 * Returns ESP_ERR_INVALID_SIZE when the file does not fit.
 */
esp_err_t storage_read(const char *relative_path, char *buffer, size_t buffer_len,
                       size_t *out_len);

/** True when the file exists. */
bool storage_exists(const char *relative_path);
