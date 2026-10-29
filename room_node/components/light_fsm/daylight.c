/**
 * @file daylight.c
 * @brief Hysteresis helper for the LDR.
 * @author Opiyo Oscar
 * @requirements FR-LGT-03, NFR-11
 */

#include "daylight.h"

#include "cocis_types.h"

bool cocis_daylight_update(bool currently_daylight, int lux)
{
    /* Bright enough: switch the decision to "daylight" and keep the lamp off. */
    if (lux >= COCIS_LUX_OFF) {
        return true;
    }

    /* Dark enough: clear the decision so motion can turn the lamp on. */
    if (lux <= COCIS_LUX_ON) {
        return false;
    }

    /* Between the thresholds: keep the previous decision (hysteresis). */
    return currently_daylight;
}
