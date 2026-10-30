/**
 * @file app.h
 * @brief Entrance hub application context and orchestration API.
 *
 * `main.c` only starts the application; everything else lives here so the
 * business logic is separate from FreeRTOS plumbing.
 *
 * @author Opiyo Oscar
 * @requirements FR-ACC-01..09, FR-SYN-01..07, FR-EMG-01..04
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "alerts.h"
#include "auth.h"
#include "cocis_log_chain.h"
#include "cocis_net.h"
#include "cocis_proto.h"
#include "cocis_time.h"
#include "cocis_types.h"
#include "door_fsm.h"
#include "emergency.h"
#include "hub_hal.h"
#include "log_store.h"
#include "occupancy.h"
#include "scan_filter.h"
#include "user_registry.h"

/* Last telemetry seen from a room node (index 0 is unused). */
typedef struct {
    bool known;
    bool online;
    bool occupied;
    bool lamp;
    int lux;
    uint32_t last_seen_ms;
} hub_room_t;

typedef struct {
    cocis_net_t net;
    cocis_auth_t auth;
    cocis_registry_t registry;
    cocis_schedule_t schedule;
    cocis_occupancy_t occupancy;
    cocis_emergency_t emergency;
    cocis_roll_call_t roll_call;
    cocis_alerts_t alerts;
    cocis_scan_filter_t scans;
    cocis_door_fsm_t door_entry;
    cocis_door_fsm_t door_exit;
    cocis_log_store_t logs;
    cocis_event_id_t ids;
    cocis_dedupe_t dedupe;
    hub_room_t rooms[3];
    bool schedule_known;
    bool schedule_open;
} hub_app_t;

/** Global application instance used by the CLI and the tasks. */
hub_app_t *hub_app_instance(void);

/** Initialise every subsystem and start the background tasks. */
void hub_app_start(void);

/** Process one (simulated or real) RFID scan. */
void hub_app_scan(hub_app_t *app, const char *uid, cocis_dir_t direction);

/** Enter or leave emergency mode. */
void hub_app_set_emergency(hub_app_t *app, bool active, const char *source);

/** Append an audit record. */
void hub_app_audit(hub_app_t *app, const char *actor, const char *action,
                   const char *target, const char *old_value,
                   const char *new_value, const char *result);

/** Publish a typed event envelope on the hub event topic. */
void hub_app_publish_event(hub_app_t *app, const char *type, const char *data_json);

/** Persist the user registry to /sdcard/config/users.csv. */
void hub_app_save_users(hub_app_t *app);

/** Validate, store and broadcast a new schedule. */
void hub_app_apply_schedule(hub_app_t *app, const cocis_schedule_t *schedule);

/** Publish the retained schedule document rooms use for local decisions. */
void hub_app_publish_schedule(hub_app_t *app);

/** Update the cached room telemetry (room 1 or 2). */
void hub_app_note_room(hub_app_t *app, int room, bool online, bool occupied,
                       bool lamp, int lux);

/** True when a room has reported within the last 15 seconds. */
bool hub_app_room_online(const hub_app_t *app, int room);

/** Remove a UID from occupancy, open the exit and audit the release. */
bool hub_app_release(hub_app_t *app, const char *uid, const char *actor);

/** Send a lamp command to a room ("on"/"off"/"auto"). */
void hub_app_set_light(hub_app_t *app, int room, const char *mode, const char *actor);

/** Milliseconds since boot (32-bit, wrap-safe for timeouts). */
uint32_t hub_now_ms(void);

/** UTC epoch seconds; 0 before SNTP synchronisation. */
uint64_t hub_now_utc(void);

/** True once the clock has a plausible date. */
bool hub_time_valid(void);

/** "VIEWER" / "SECURITY" / "ADMIN". */
const char *hub_role_str(cocis_role_t role);

/** "ACTIVE" / "SUSPENDED" / "EXPIRED". */
const char *hub_status_str(cocis_user_status_t status);
