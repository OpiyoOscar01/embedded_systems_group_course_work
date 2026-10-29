/**
 * @file light_fsm.h
 * @brief Room lighting state machine: occupancy, daylight, warning and override.
 *
 * Behaviour implemented here (all times in milliseconds):
 *   - motion marks the room occupied and turns the lamp on when it is dark;
 *   - after T_VAC without motion the lamp enters a warning phase;
 *   - during T_WARN the lamp blinks once per second (500 ms on/off);
 *   - after the warning the lamp turns off;
 *   - motion at any time cancels the warning and keeps the lamp on;
 *   - sufficient daylight switches the lamp off immediately;
 *   - a manual override forces the lamp on for a bounded time.
 *
 * The module is pure logic: the caller supplies the clock and sensor values,
 * which keeps it host-testable and makes the timing deterministic.
 *
 * @author Opiyo Oscar
 * @requirements FR-LGT-01..05, FR-LGT-08, FR-LGT-09
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COCIS_LIGHT_OFF = 0,
    COCIS_LIGHT_ON,
    COCIS_LIGHT_WARNING,
} cocis_light_state_t;

/* Tunables. Zero values fall back to the Appendix C defaults. */
typedef struct {
    uint32_t t_vac_s;       /* vacancy timeout before warning */
    uint32_t t_warn_s;      /* warning/blink duration */
    uint32_t override_s;    /* manual override duration */
} cocis_light_config_t;

typedef struct {
    /* Resolved durations in milliseconds, derived from the config above. */
    uint32_t t_vac_ms;
    uint32_t t_warn_ms;
    uint32_t override_ms;

    bool occupied;              /* motion hold is active */
    bool override_active;       /* manual override is active */
    bool daylight;              /* last daylight decision (hysteresis output) */
    bool relay_on;              /* requested lamp state */
    bool initialized;
    cocis_light_state_t state;
    uint32_t last_motion_ms;
    uint32_t warning_started_ms;
    uint32_t override_until_ms;
} cocis_light_fsm_t;

/**
 * Initialise the FSM in the OFF state.
 * `config` may be NULL or contain zeros to use the defaults for any field.
 */
void cocis_light_init(cocis_light_fsm_t *fsm, const cocis_light_config_t *config);

/**
 * Advance the FSM with the current sensor snapshot.
 * Returns the requested relay state (true = lamp on).
 */
bool cocis_light_update(cocis_light_fsm_t *fsm, uint32_t now_ms,
                        bool motion, bool daylight);

/**
 * Start a manual override. A zero duration uses the configured/default value.
 * The override is bounded so a forgotten CLI command cannot waste power.
 */
void cocis_light_override(cocis_light_fsm_t *fsm, uint32_t now_ms, uint32_t duration_ms);

/** Remaining override time, or 0 when no override is active. */
uint32_t cocis_light_override_remaining_ms(const cocis_light_fsm_t *fsm, uint32_t now_ms);

/** True when the lamp is in the pre-off warning (blinking) phase. */
bool cocis_light_is_warning(const cocis_light_fsm_t *fsm);
