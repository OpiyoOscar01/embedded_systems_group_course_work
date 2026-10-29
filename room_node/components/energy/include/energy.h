/**
 * @file energy.h
 * @brief Lamp-on time and energy estimate accounting.
 *
 * The room node integrates lamp-on seconds and a baseline (the time the lamp
 * would have been on without automation). The difference is the estimated
 * saving. Arithmetic is integer-only so results are deterministic and cheap
 * on the ESP32.
 *
 * @author Opiyo Oscar
 * @requirements FR-LGT-06, FR-LGT-07
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t lamp_on_s;    /* seconds the lamp actually ran */
    uint64_t baseline_s;   /* seconds the lamp would have run without automation */
} cocis_energy_t;

/** Reset counters to zero. */
void cocis_energy_init(cocis_energy_t *energy);

/**
 * Integrate one interval.
 * @param elapsed_s seconds since the previous call
 * @param lamp_on true when the lamp was on during the interval
 * @param baseline_on true when the interval counts toward the baseline
 */
void cocis_energy_add(cocis_energy_t *energy, uint32_t elapsed_s,
                      bool lamp_on, bool baseline_on);

/** Estimated energy used, in milliwatt-hours, for a lamp of `lamp_w` watts. */
uint64_t cocis_energy_mwh_used(const cocis_energy_t *energy, uint32_t lamp_w);

/** Estimated energy saved versus the baseline, in milliwatt-hours. */
uint64_t cocis_energy_mwh_saved(const cocis_energy_t *energy, uint32_t lamp_w);
