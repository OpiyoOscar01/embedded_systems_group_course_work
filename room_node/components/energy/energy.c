/**
 * @file energy.c
 * @brief Integer energy accounting (pure logic).
 * @author Opiyo Oscar
 * @requirements FR-LGT-06, FR-LGT-07
 */

#include "energy.h"

#include "cocis_types.h"

void cocis_energy_init(cocis_energy_t *energy)
{
    if (energy == NULL) {
        return;
    }
    energy->lamp_on_s = 0;
    energy->baseline_s = 0;
}

void cocis_energy_add(cocis_energy_t *energy, uint32_t elapsed_s,
                      bool lamp_on, bool baseline_on)
{
    if (energy == NULL) {
        return;
    }

    if (lamp_on) {
        energy->lamp_on_s += elapsed_s;
    }
    if (baseline_on) {
        energy->baseline_s += elapsed_s;
    }
}

/**
 * Convert watt-seconds to milliwatt-hours without floating point:
 *   Wh = s * W / 3600  ->  mWh = s * W * 1000 / 3600
 */
static uint64_t seconds_to_mwh(uint64_t seconds, uint32_t lamp_w)
{
    return (seconds * (uint64_t)lamp_w * 1000ull) / 3600ull;
}

uint64_t cocis_energy_mwh_used(const cocis_energy_t *energy, uint32_t lamp_w)
{
    if (energy == NULL) {
        return 0;
    }
    return seconds_to_mwh(energy->lamp_on_s, lamp_w);
}

uint64_t cocis_energy_mwh_saved(const cocis_energy_t *energy, uint32_t lamp_w)
{
    if (energy == NULL || energy->baseline_s <= energy->lamp_on_s) {
        return 0;
    }
    return seconds_to_mwh(energy->baseline_s - energy->lamp_on_s, lamp_w);
}
