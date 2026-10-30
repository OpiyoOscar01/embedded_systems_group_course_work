/**
 * @file hub_hal.h
 * @brief Hardware abstraction for the entrance hub.
 *
 * All GPIO/LEDC access for the hub lives here (NFR-10): the rest of the code
 * calls these functions and never touches a pin directly. Pin numbers match
 * Table 20 of the requirements document.
 *
 * The RFID readers are intentionally not driven over SPI: the course demo
 * injects scans through the console/HAL so the access logic can be shown
 * without extra reader hardware. The SPI bus is still initialised for the SD
 * card by the storage component.
 *
 * @author Opiyo Oscar
 * @requirements NFR-10, FR-ACC-03, FR-EMG-01, FR-ALR-03
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Pin map (Table 20). Kept as macros so the diagram and code stay in sync. */
#define HUB_PIN_SD_CS          5
#define HUB_PIN_RFID_ENTRY_CS  27
#define HUB_PIN_RFID_EXIT_CS   26
#define HUB_PIN_RFID_RST       25
#define HUB_PIN_I2C_SDA        21
#define HUB_PIN_I2C_SCL        22

#define HUB_PIN_SERVO_ENTRY    13
#define HUB_PIN_SERVO_EXIT     14

#define HUB_PIN_LED_ENTRY_GREEN 32
#define HUB_PIN_LED_ENTRY_RED   33
#define HUB_PIN_LED_EXIT_GREEN  16
#define HUB_PIN_LED_EXIT_RED    17

#define HUB_PIN_BUZZER         4
#define HUB_PIN_BUTTON_EMERGENCY 34
#define HUB_PIN_BUTTON_TAMPER    39
#define HUB_PIN_PASSAGE          35

typedef enum {
    HUB_DOOR_ENTRY = 0,
    HUB_DOOR_EXIT = 1,
} hub_door_t;

/* Result of the power-on self test. */
typedef struct {
    bool gpio_ok;
    bool servo_ok;
    bool buzzer_ok;
    bool buttons_ok;
    bool oled_ok;
} hub_diag_t;

/** Configure every hub peripheral. Safe to call once at boot. */
esp_err_t hub_hal_init(void);

/**
 * Show the access result on the door's green/red LEDs.
 * Granted/emergency/anomaly use green; denials use red.
 */
void hub_hal_show_result(hub_door_t door, bool granted);

/** Turn both LEDs of one door off. */
void hub_hal_leds_off(hub_door_t door);

/** Move the door servo: true = unlocked (2 ms), false = locked (1 ms). */
void hub_hal_set_door(hub_door_t door, bool open);

/** Non-blocking buzzer beep. Repeated calls restart the timer. */
void hub_hal_beep(uint32_t duration_ms);

/** Read the optional passage sensor (true = somebody passed). */
bool hub_hal_passage_detected(void);

/**
 * Consume a debounced emergency-button press.
 * The ISR only sets a flag; the main task calls this to act on the event.
 */
bool hub_hal_take_emergency_press(void);

/** Consume a debounced tamper-switch event. */
bool hub_hal_take_tamper_event(void);

/** Return the last self-test result. */
hub_diag_t hub_hal_diag(void);

/** True when the optional SSD1306 panel answered on I2C. */
bool hub_hal_display_ready(void);

/**
 * Draw four short lines on the status OLED (no-op when absent).
 * Text is upper-cased and truncated to 21 characters per line.
 */
void hub_hal_display_show(const char *line1, const char *line2,
                          const char *line3, const char *line4);
