/**
 * @file room_hal.h
 * @brief Hardware abstraction for a room node.
 *
 * Both rooms run this same code; only CONFIG_COCIS_ROOM_ID differs. All
 * GPIO/ADC access lives here (NFR-10). Pin numbers match Table 21.
 *
 * @author Opiyo Oscar
 * @requirements NFR-10, FR-LGT-01, FR-LGT-05, FR-LGT-06
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

/* Pin map (Table 21). */
#define ROOM_PIN_PIR             13
#define ROOM_PIN_OVERRIDE_BUTTON 14
#define ROOM_PIN_RELAY           26
#define ROOM_PIN_LED_OCCUPIED    25
#define ROOM_PIN_LED_LIGHTS      33
#define ROOM_PIN_LED_NETWORK     32
#define ROOM_PIN_I2C_SDA         21
#define ROOM_PIN_I2C_SCL         22

/* ADC1 channel 6 is GPIO34. ADC2 must not be used while Wi-Fi is active. */
#define ROOM_LDR_ADC_CHANNEL     ADC_CHANNEL_6

typedef struct {
    bool gpio_ok;
    bool adc_ok;
    bool button_ok;
} room_diag_t;

/** Configure every room peripheral. Call once at boot. */
esp_err_t room_hal_init(void);

/** True while the PIR reports motion. */
bool room_hal_read_motion(void);

/**
 * Read the LDR and convert the raw ADC value to an approximate lux value.
 * The mapping is linear and intended for threshold decisions, not metrology.
 */
int room_hal_read_lux(void);

/** Drive the lamp relay (true = lamp on). */
void room_hal_set_relay(bool on);

/** Update the three status LEDs. */
void room_hal_set_led_occupied(bool on);
void room_hal_set_led_lights(bool on);
void room_hal_set_led_network(bool on);

/** Consume a debounced manual-override button press. */
bool room_hal_take_override_press(void);

/** True when the optional SSD1306 status display is present. */
bool room_hal_display_ready(void);

/** Render four short lines on the room status display. */
void room_hal_display_show(const char *line1, const char *line2,
                           const char *line3, const char *line4);

/** Last self-test result. */
room_diag_t room_hal_diag(void);
