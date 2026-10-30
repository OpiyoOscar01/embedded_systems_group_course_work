/**
 * @file app.h
 * @brief Room node application context and orchestration API.
 * @author Opiyo Oscar
 * @requirements FR-LGT-01..09, FR-SYN-01..07
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cocis_net.h"
#include "cocis_proto.h"
#include "cocis_time.h"
#include "energy.h"
#include "light_fsm.h"

typedef struct {
    cocis_net_t net;
    cocis_light_fsm_t light;
    cocis_energy_t energy;
    cocis_schedule_t schedule;      /* latest version from the hub */
    cocis_event_id_t ids;
    char node[8];                   /* "R1" / "R2" */
    bool last_relay;
    uint32_t last_energy_ms;
    uint32_t last_energy_log_ms;
    uint32_t last_intrusion_ms;

    /* Console injection used when no PIR/LDR part is wired in the demo. */
    bool light_forced_off;
    bool sim_motion_active;
    bool sim_motion_value;
    bool sim_lux_active;
    int sim_lux;
} room_app_t;

/** Global instance for the console and tasks. */
room_app_t *room_app_instance(void);

/** Initialise peripherals/network and start the control task. */
void room_app_start(void);

/** Force a manual override for `minutes` (0 = configured default). */
void room_app_override(room_app_t *app, uint32_t minutes);

/** "on" (bounded override), "off" (force off) or "auto" (cancel override). */
void room_app_set_light(room_app_t *app, const char *mode);

/** True when the current local time is inside the configured schedule. */
bool room_app_is_open(const room_app_t *app);

/** Console sensor injection; `active=false` returns to the real sensor. */
void room_app_sim_motion(room_app_t *app, bool active, bool value);
void room_app_sim_lux(room_app_t *app, bool active, int value);

/** Milliseconds since boot. */
uint32_t room_now_ms(void);

/** True when the clock is synchronised. */
bool room_time_valid(void);
