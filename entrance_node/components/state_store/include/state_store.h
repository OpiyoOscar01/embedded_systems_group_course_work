/**
 * @file state_store.h
 * @brief NVS persistence for schedule and occupancy.
 *
 * Pure logic lives in cocis_time/occupancy; this component is the thin,
 * target-only bridge to NVS. It also mirrors the schedule to the SD card so
 * an examiner can inspect it without a console.
 *
 * @author Opiyo Oscar
 * @requirements FR-SCH-06, FR-OCC-05, NFR-05
 */

#pragma once

#include "auth.h"
#include "cocis_time.h"
#include "occupancy.h"
#include "esp_err.h"

/** Load the schedule, falling back to the built-in default. */
esp_err_t state_store_load_schedule(cocis_schedule_t *schedule);

/** Save the schedule to NVS and (best effort) /sdcard/config/schedule.json. */
esp_err_t state_store_save_schedule(const cocis_schedule_t *schedule);

/** Load the occupancy table; a missing/corrupt blob yields an empty table. */
esp_err_t state_store_load_occupancy(cocis_occupancy_t *occupancy);

/** Persist the occupancy table so a reboot does not lose who is inside. */
esp_err_t state_store_save_occupancy(const cocis_occupancy_t *occupancy);

/** Load console credentials (salt + PBKDF2 hash only; never passwords). */
esp_err_t state_store_load_auth(cocis_auth_t *auth);

/** Save console credentials after provisioning or a password change. */
esp_err_t state_store_save_auth(const cocis_auth_t *auth);
