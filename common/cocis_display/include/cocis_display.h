#pragma once

#include <stdbool.h>

#include "esp_err.h"

/** Initialise a 128x64 SSD1306 panel on the given I2C pins. */
esp_err_t cocis_display_init(int sda_gpio, int scl_gpio);

/** True when the panel was detected and configured. */
bool cocis_display_ready(void);

/** Render up to four short lines (max 21 characters each). */
void cocis_display_show(const char *line1, const char *line2,
                        const char *line3, const char *line4);
