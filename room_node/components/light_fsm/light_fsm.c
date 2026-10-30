/**
 * @file light_fsm.c
 * @brief Lighting state machine implementation (pure logic).
 * @author Opiyo Oscar
 * @requirements FR-LGT-01..05, FR-LGT-08, FR-LGT-09
 */

#include "light_fsm.h"

#include "cocis_types.h"

/** Warning blink half-period: 500 ms gives a 1 Hz on/off cycle. */
#define COCIS_BLINK_HALF_MS 500u

/** Convert seconds to milliseconds with a fallback for zero. */
static uint32_t seconds_or(uint32_t value_s, uint32_t fallback_s)
{
    uint32_t seconds = (value_s == 0) ? fallback_s : value_s;
    return seconds * 1000u;
}

void cocis_light_init(cocis_light_fsm_t *fsm, const cocis_light_config_t *config)
{
    if (fsm == NULL) {
        return;
    }

    /* Start from the Appendix C defaults, then let the caller override
     * individual fields. A zero field keeps its default. */
    cocis_light_config_t requested = {
        .t_vac_s = COCIS_T_VAC_S,
        .t_warn_s = COCIS_T_WARN_S,
        .override_s = COCIS_OVERRIDE_MIN * 60u,
    };
    if (config != NULL) {
        requested = *config;
    }

    fsm->t_vac_ms = seconds_or(requested.t_vac_s, COCIS_T_VAC_S);
    fsm->t_warn_ms = seconds_or(requested.t_warn_s, COCIS_T_WARN_S);
    fsm->override_ms = seconds_or(requested.override_s, COCIS_OVERRIDE_MIN * 60u);

    fsm->occupied = false;
    fsm->override_active = false;
    fsm->daylight = false;
    fsm->relay_on = false;
    fsm->initialized = true;
    fsm->state = COCIS_LIGHT_OFF;
    fsm->last_motion_ms = 0;
    fsm->warning_started_ms = 0;
    fsm->override_until_ms = 0;
}

bool cocis_light_update(cocis_light_fsm_t *fsm, uint32_t now_ms,
                        bool motion, bool daylight)
{
    if (fsm == NULL || !fsm->initialized) {
        return false;
    }

    fsm->daylight = daylight;

    /* Expire the override before deciding the output. Unsigned subtraction
     * makes this safe across the 32-bit millisecond wrap. */
    if (fsm->override_active && (now_ms - fsm->override_until_ms) < 0x80000000u) {
        fsm->override_active = false;
    }

    /* Motion refreshes the hold timer and immediately cancels a warning. */
    if (motion) {
        fsm->last_motion_ms = now_ms;
        fsm->occupied = true;
        if (fsm->state == COCIS_LIGHT_WARNING) {
            fsm->state = COCIS_LIGHT_ON;
        }
    }

    /* End the occupancy hold after T_VAC of silence. */
    bool vacated_now = false;
    if (fsm->occupied && (now_ms - fsm->last_motion_ms) >= fsm->t_vac_ms) {
        fsm->occupied = false;
        vacated_now = true;
    }

    /* Manual override has the highest priority after motion bookkeeping. */
    if (fsm->override_active) {
        fsm->state = COCIS_LIGHT_ON;
        fsm->relay_on = true;
        return true;
    }

    /* Enough daylight: keep the lamp off even if the room is occupied. */
    if (daylight) {
        fsm->state = COCIS_LIGHT_OFF;
        fsm->relay_on = false;
        return false;
    }

    /* Occupied and dark: lamp on. */
    if (fsm->occupied) {
        fsm->state = COCIS_LIGHT_ON;
        fsm->relay_on = true;
        return true;
    }

    /* Warning phase: blink until the warning time expires. */
    if (fsm->state == COCIS_LIGHT_WARNING) {
        uint32_t elapsed = now_ms - fsm->warning_started_ms;
        if (elapsed >= fsm->t_warn_ms) {
            fsm->state = COCIS_LIGHT_OFF;
            fsm->relay_on = false;
            return false;
        }

        bool on = ((elapsed / COCIS_BLINK_HALF_MS) % 2u) == 0;
        fsm->relay_on = on;
        return on;
    }

    /* The room just became vacant: begin the pre-off warning. */
    if (vacated_now) {
        fsm->state = COCIS_LIGHT_WARNING;
        fsm->warning_started_ms = now_ms;
        fsm->relay_on = true;
        return true;
    }

    fsm->state = COCIS_LIGHT_OFF;
    fsm->relay_on = false;
    return false;
}

void cocis_light_override(cocis_light_fsm_t *fsm, uint32_t now_ms, uint32_t duration_ms)
{
    if (fsm == NULL || !fsm->initialized) {
        return;
    }

    if (duration_ms == 0) {
        duration_ms = fsm->override_ms;
    }

    fsm->override_active = true;
    fsm->override_until_ms = now_ms + duration_ms;
    fsm->state = COCIS_LIGHT_ON;
    fsm->relay_on = true;
}

uint32_t cocis_light_override_remaining_ms(const cocis_light_fsm_t *fsm, uint32_t now_ms)
{
    if (fsm == NULL || !fsm->override_active) {
        return 0;
    }

    /* A wrapped deadline yields a huge value; clamp it to zero. */
    uint32_t elapsed = now_ms - fsm->override_until_ms;
    if (elapsed < 0x80000000u) {
        return 0;
    }
    return fsm->override_until_ms - now_ms;
}

void cocis_light_cancel_override(cocis_light_fsm_t *fsm)
{
    if (fsm == NULL) {
        return;
    }
    fsm->override_active = false;
    fsm->override_until_ms = 0;
}

void cocis_light_force_off(cocis_light_fsm_t *fsm)
{
    if (fsm == NULL) {
        return;
    }
    fsm->override_active = false;
    fsm->override_until_ms = 0;
    fsm->occupied = false;
    fsm->state = COCIS_LIGHT_OFF;
    fsm->relay_on = false;
}

bool cocis_light_is_warning(const cocis_light_fsm_t *fsm)
{
    return fsm != NULL && fsm->state == COCIS_LIGHT_WARNING;
}
