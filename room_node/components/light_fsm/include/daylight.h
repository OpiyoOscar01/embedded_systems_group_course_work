/**
 * @file daylight.h
 * @brief LDR daylight decision with hysteresis.
 *
 * The lamp must not chatter when the light level sits near a single threshold.
 * Two thresholds are used: below LUX_ON it is dark, above LUX_OFF it is bright,
 * and between them the previous decision is kept.
 *
 * @author Opiyo Oscar
 * @requirements FR-LGT-03, NFR-11
 */

#pragma once

#include <stdbool.h>

/**
 * Update the daylight state.
 * @param currently_daylight previous decision
 * @param lux measured illuminance
 * @return true when there is enough daylight to keep the lamp off
 */
bool cocis_daylight_update(bool currently_daylight, int lux);
