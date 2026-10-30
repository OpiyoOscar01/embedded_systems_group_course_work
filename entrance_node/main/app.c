/**
 * @file app.c
 * @brief Entrance hub orchestration: scans, doors, telemetry, emergency.
 * @author Opiyo Oscar
 * @requirements FR-ACC-01..09, FR-SYN-01..07, FR-EMG-01..04, FR-STO-05
 */

#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "access_ctrl.h"
#include "auth_crypto.h"
#include "cli.h"
#include "cocis_time_sntp.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "state_store.h"
#include "storage.h"

static const char *TAG = "hub_app";

static hub_app_t s_app;

/* ------------------------------------------------------------------------- */
/* Clock helpers                                                              */
/* ------------------------------------------------------------------------- */

uint32_t hub_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

uint64_t hub_now_utc(void)
{
    time_t now = time(NULL);
    return now > 0 ? (uint64_t)now : 0;
}

bool hub_time_valid(void)
{
    int year = 0;
    return cocis_time_now_eat(&year, NULL, NULL, NULL, NULL, NULL) &&
           year >= COCIS_TIME_MIN_VALID_YEAR;
}

/** Build "YYYYMMDD" (UTC) and "YYYY-MM-DDTHH:MM:SS" (UTC). */
static void now_strings(char date[9], char stamp[20])
{
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(date, 9, "%Y%m%d", &utc);
    strftime(stamp, 20, "%Y-%m-%dT%H:%M:%S", &utc);
}

const char *hub_role_str(cocis_role_t role)
{
    switch (role) {
    case COCIS_ROLE_VIEWER:   return "VIEWER";
    case COCIS_ROLE_SECURITY: return "SECURITY";
    case COCIS_ROLE_ADMIN:    return "ADMIN";
    default:                  return "UNKNOWN";
    }
}

const char *hub_status_str(cocis_user_status_t status)
{
    switch (status) {
    case COCIS_USER_ACTIVE:    return "ACTIVE";
    case COCIS_USER_SUSPENDED: return "SUSPENDED";
    case COCIS_USER_EXPIRED:   return "EXPIRED";
    default:                   return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------------- */
/* Audit and event publish                                                    */
/* ------------------------------------------------------------------------- */

void hub_app_audit(hub_app_t *app, const char *actor, const char *action,
                   const char *target, const char *old_value,
                   const char *new_value, const char *result)
{
    char date[9];
    char stamp[20];
    now_strings(date, stamp);
    cocis_log_store_audit(&app->logs, date, stamp, actor, action, target,
                          old_value, new_value, result);
}

void hub_app_publish_event(hub_app_t *app, const char *type, const char *data_json)
{
    cocis_envelope_t envelope = {0};
    envelope.version = COCIS_PROTO_VERSION;
    envelope.ts = hub_now_utc();
    snprintf(envelope.src, sizeof(envelope.src), "HUB");
    snprintf(envelope.type, sizeof(envelope.type), "%s", type);
    if (!cocis_event_id_next(&app->ids, envelope.id, sizeof(envelope.id))) {
        return;
    }

    char payload[COCIS_NET_PAYLOAD_MAX];
    int length = cocis_proto_encode(payload, sizeof(payload), &envelope, data_json);
    if (length <= 0) {
        ESP_LOGW(TAG, "event encode failed");
        return;
    }

    char topic[COCIS_NET_TOPIC_MAX];
    if (!cocis_proto_topic_node(topic, sizeof(topic), CONFIG_COCIS_GROUP_PREFIX,
                                "HUB", "event")) {
        return;
    }
    cocis_net_publish(&app->net, topic, payload, 1, false);
}

/* ------------------------------------------------------------------------- */
/* Registry persistence                                                       */
/* ------------------------------------------------------------------------- */

static void load_users(hub_app_t *app)
{
    cocis_registry_init(&app->registry);

    static char buffer[4096];
    size_t length = 0;
    if (storage_read("config/users.csv", buffer, sizeof(buffer), &length) == ESP_OK) {
        char *save = NULL;
        for (char *line = strtok_r(buffer, "\n", &save); line != NULL;
             line = strtok_r(NULL, "\n", &save)) {
            cocis_user_t user;
            if (cocis_user_parse_csv(line, &user)) {
                cocis_registry_add(&app->registry, &user);
            }
        }
        if (app->registry.count > 0) {
            ESP_LOGI(TAG, "loaded %u users", (unsigned)app->registry.count);
            return;
        }
    }

    /* First boot: one demo tag so the Wokwi demo can show a grant. */
    cocis_user_t demo = {0};
    snprintf(demo.uid, sizeof(demo.uid), "A1B2C3D4");
    snprintf(demo.name, sizeof(demo.name), "Demo Student");
    demo.role = COCIS_ROLE_VIEWER;
    demo.status = COCIS_USER_ACTIVE;
    snprintf(demo.created_at, sizeof(demo.created_at), "2026-10-29T00:00:00");
    cocis_registry_add(&app->registry, &demo);
    hub_app_save_users(app);
}

void hub_app_save_users(hub_app_t *app)
{
    static char buffer[4096];
    size_t used = 0;
    buffer[0] = '\0';

    for (size_t i = 0; i < app->registry.count; i++) {
        char line[128];
        int length = cocis_user_format_csv(&app->registry.users[i], line, sizeof(line));
        if (length <= 0 || used + (size_t)length + 2 >= sizeof(buffer)) {
            break;
        }
        memcpy(buffer + used, line, (size_t)length);
        used += (size_t)length;
        buffer[used++] = '\n';
        buffer[used] = '\0';
    }

    if (storage_ready()) {
        storage_write("config/users.csv", buffer);
    }
}

/* ------------------------------------------------------------------------- */
/* Credential provisioning                                                    */
/* ------------------------------------------------------------------------- */

static bool add_default_user(cocis_auth_t *auth, const char *name, cocis_role_t role,
                             const char *password, bool must_change)
{
    uint8_t salt[COCIS_AUTH_SALT_LEN];
    uint8_t hash[COCIS_AUTH_HASH_LEN];

    cocis_auth_random_salt(salt);
    if (!cocis_auth_pbkdf2_sha256(password, salt, sizeof(salt),
                                  COCIS_AUTH_ITERATIONS, hash, sizeof(hash))) {
        return false;
    }
    return cocis_auth_add(auth, name, role, salt, hash, must_change);
}

static void provision_auth(hub_app_t *app)
{
    cocis_auth_init(&app->auth);
    bool ok = add_default_user(&app->auth, "admin", COCIS_ROLE_ADMIN, "admin123", true) &&
              add_default_user(&app->auth, "security", COCIS_ROLE_SECURITY,
                               "security123", false) &&
              add_default_user(&app->auth, "viewer", COCIS_ROLE_VIEWER, "viewer123", false);
    if (ok) {
        state_store_save_auth(&app->auth);
        ESP_LOGW(TAG, "provisioned default console users");
    } else {
        ESP_LOGE(TAG, "failed to provision console users");
    }
}

/* ------------------------------------------------------------------------- */
/* Access handling                                                            */
/* ------------------------------------------------------------------------- */

/** Apply the occupancy side effect of an access decision. */
static void apply_occupancy(hub_app_t *app, const char *uid, cocis_dir_t direction,
                            cocis_result_t result)
{
    bool granted = result == COCIS_RES_GRANTED ||
                   result == COCIS_RES_EMERGENCY_PASS ||
                   result == COCIS_RES_ANOMALY_EXIT_NO_ENTRY;
    if (!granted) {
        return;
    }

    if (direction == COCIS_DIR_ENTRY) {
        if (cocis_occupancy_enter(&app->occupancy, uid, (uint32_t)hub_now_utc())) {
            state_store_save_occupancy(&app->occupancy);
        }
    } else {
        if (cocis_occupancy_leave(&app->occupancy, uid)) {
            state_store_save_occupancy(&app->occupancy);
        }
    }
}

void hub_app_scan(hub_app_t *app, const char *uid, cocis_dir_t direction)
{
    if (app == NULL || uid == NULL || uid[0] == '\0') {
        return;
    }

    uint32_t now_ms = hub_now_ms();
    if (!cocis_scan_filter_accept(&app->scans, uid, now_ms)) {
        ESP_LOGI(TAG, "duplicate scan of %s ignored", uid);
        return;
    }

    const cocis_user_t *user = cocis_registry_find(&app->registry, uid);

    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    bool time_valid = cocis_time_now_eat(&year, &month, &day, &hour, &minute, NULL);
    cocis_schedule_state_t schedule_state =
        cocis_schedule_eval(&app->schedule, time_valid, year, month, day, hour, minute);

    bool inside = cocis_occupancy_contains(&app->occupancy, uid);
    cocis_access_input_t input = {
        .user_known = user != NULL,
        .user_status = user != NULL ? cocis_user_effective_status(user, year, month, day)
                                    : COCIS_USER_ACTIVE,
        .direction = direction,
        .schedule_state = schedule_state,
        .emergency = app->emergency.active,
        .inside = inside,
    };
    cocis_access_decision_t decision = cocis_access_evaluate(&input);

    apply_occupancy(app, uid, direction, decision.result);

    hub_door_t door = direction == COCIS_DIR_ENTRY ? HUB_DOOR_ENTRY : HUB_DOOR_EXIT;
    cocis_door_fsm_t *fsm = direction == COCIS_DIR_ENTRY ? &app->door_entry : &app->door_exit;

    hub_hal_show_result(door, decision.open_door);
    if (decision.open_door) {
        hub_hal_beep(120);
        if (cocis_door_fsm_request(fsm, now_ms)) {
            hub_hal_set_door(door, true);
        }
        cocis_alerts_clear_uid(&app->alerts, uid);
    } else {
        if (cocis_alerts_note_denial(&app->alerts, uid)) {
            char alert[160];
            snprintf(alert, sizeof(alert),
                     "{\"kind\":\"bruteforce\",\"uid\":\"%s\",\"count\":%d}",
                     uid, COCIS_DENIAL_THRESHOLD);
            hub_app_publish_event(app, "alert", alert);
            hub_app_audit(app, "system", "ALERT", uid, "", "", "BRUTEFORCE");
            hub_hal_beep(600);
        }
    }

    /* Access log (hash chained). */
    char date[9];
    char stamp[20];
    now_strings(date, stamp);
    cocis_log_store_access(&app->logs, date, stamp, stamp, "HUB", uid,
                           user != NULL ? user->name : "UNKNOWN",
                           direction, decision.result,
                           cocis_result_str(decision.result));

    /* Real-time event for the dashboard/other nodes. */
    char event[256];
    snprintf(event, sizeof(event),
             "{\"uid\":\"%s\",\"name\":\"%s\",\"dir\":\"%s\",\"res\":\"%s\","
             "\"reason\":\"%s\",\"count\":%u,\"emergency\":%s}",
             uid,
             user != NULL ? user->name : "UNKNOWN",
             direction == COCIS_DIR_ENTRY ? "IN" : "OUT",
             cocis_result_str(decision.result),
             cocis_schedule_state_str(schedule_state),
             (unsigned)cocis_occupancy_count(&app->occupancy),
             app->emergency.active ? "true" : "false");
    hub_app_publish_event(app, "access", event);

    ESP_LOGI(TAG, "%s %s -> %s (%u inside)", uid,
             direction == COCIS_DIR_ENTRY ? "IN" : "OUT",
             cocis_result_str(decision.result),
             (unsigned)cocis_occupancy_count(&app->occupancy));
}

/* ------------------------------------------------------------------------- */
/* Emergency                                                                  */
/* ------------------------------------------------------------------------- */

void hub_app_set_emergency(hub_app_t *app, bool active, const char *source)
{
    if (app == NULL) {
        return;
    }

    if (active && !app->emergency.active) {
        cocis_emergency_activate(&app->emergency, (uint32_t)hub_now_utc(),
                                 source != NULL ? source : "BUTTON");
        cocis_roll_call_capture(&app->roll_call, &app->occupancy,
                                (uint32_t)hub_now_utc());
        hub_hal_set_door(HUB_DOOR_ENTRY, true);
        hub_hal_set_door(HUB_DOOR_EXIT, true);
        hub_hal_beep(1000);

        char event[160];
        snprintf(event, sizeof(event),
                 "{\"active\":true,\"source\":\"%s\",\"count\":%u}",
                 app->emergency.source, (unsigned)app->roll_call.count);
        hub_app_publish_event(app, "emergency", event);
        hub_app_audit(app, "system", "EMERGENCY_ON", app->emergency.source,
                      "", "", "OK");
        ESP_LOGW(TAG, "EMERGENCY MODE (%u inside)", (unsigned)app->roll_call.count);
    } else if (!active && app->emergency.active) {
        cocis_emergency_clear(&app->emergency);
        hub_hal_set_door(HUB_DOOR_ENTRY, false);
        hub_hal_set_door(HUB_DOOR_EXIT, false);
        hub_app_publish_event(app, "emergency", "{\"active\":false}");
        hub_app_audit(app, "system", "EMERGENCY_OFF", "", "", "", "OK");
        ESP_LOGW(TAG, "emergency cleared");
    }
}

/* ------------------------------------------------------------------------- */
/* Schedule                                                                   */
/* ------------------------------------------------------------------------- */

void hub_app_apply_schedule(hub_app_t *app, const cocis_schedule_t *schedule)
{
    if (app == NULL || schedule == NULL) {
        return;
    }

    char old_version[16];
    snprintf(old_version, sizeof(old_version), "%lu",
             (unsigned long)app->schedule.version);

    app->schedule = *schedule;
    app->schedule.version++;

    state_store_save_schedule(&app->schedule);

    char new_version[16];
    snprintf(new_version, sizeof(new_version), "%lu",
             (unsigned long)app->schedule.version);
    hub_app_audit(app, "console", "SCHEDULE_SET", "weekly",
                  old_version, new_version, "OK");

    char event[64];
    snprintf(event, sizeof(event), "{\"version\":%lu}",
             (unsigned long)app->schedule.version);
    hub_app_publish_event(app, "schedule", event);

    hub_app_publish_schedule(app);
}

void hub_app_publish_schedule(hub_app_t *app)
{
    if (app == NULL) {
        return;
    }

    char topic[COCIS_NET_TOPIC_MAX];
    if (!cocis_proto_topic_node(topic, sizeof(topic), CONFIG_COCIS_GROUP_PREFIX,
                                "HUB", "schedule")) {
        return;
    }

    char json[1024];
    if (cocis_schedule_to_json(&app->schedule, json, sizeof(json)) <= 0) {
        return;
    }

    /* Retained so a room that boots later still receives the current hours. */
    cocis_net_publish(&app->net, topic, json, 1, true);
}

void hub_app_note_room(hub_app_t *app, int room, bool online, bool occupied,
                       bool lamp, int lux)
{
    if (app == NULL || room < 1 || room > 2) {
        return;
    }

    app->rooms[room].known = true;
    app->rooms[room].online = online;
    app->rooms[room].occupied = occupied;
    app->rooms[room].lamp = lamp;
    app->rooms[room].lux = lux;
    app->rooms[room].last_seen_ms = hub_now_ms();
}

bool hub_app_room_online(const hub_app_t *app, int room)
{
    if (app == NULL || room < 1 || room > 2) {
        return false;
    }

    const hub_room_t *state = &app->rooms[room];
    if (!state->known || !state->online) {
        return false;
    }

    return (hub_now_ms() - state->last_seen_ms) <= 15000u;
}

bool hub_app_release(hub_app_t *app, const char *uid, const char *actor)
{
    if (app == NULL || uid == NULL || actor == NULL) {
        return false;
    }

    if (!cocis_occupancy_contains(&app->occupancy, uid)) {
        return false;
    }

    cocis_occupancy_leave(&app->occupancy, uid);
    state_store_save_occupancy(&app->occupancy);

    cocis_door_fsm_request(&app->door_exit, hub_now_ms());
    hub_hal_set_door(HUB_DOOR_EXIT, true);
    hub_hal_show_result(HUB_DOOR_EXIT, true);
    hub_hal_beep(150);

    hub_app_audit(app, actor, "RELEASE", uid, "", "", "OK");
    hub_app_publish_event(app, "release", "{\"kind\":\"manual\"}");

    ESP_LOGW(TAG, "%s released %s", actor, uid);
    return true;
}

void hub_app_set_light(hub_app_t *app, int room, const char *mode, const char *actor)
{
    if (app == NULL || mode == NULL || room < 1 || room > 2) {
        return;
    }

    cocis_envelope_t envelope = {0};
    envelope.version = COCIS_PROTO_VERSION;
    envelope.ts = hub_now_utc();
    cocis_event_id_next(&app->ids, envelope.id, sizeof(envelope.id));
    snprintf(envelope.src, sizeof(envelope.src), "HUB");
    snprintf(envelope.type, sizeof(envelope.type), "cmd");

    char data[48];
    snprintf(data, sizeof(data), "{\"light\":\"%s\"}", mode);

    char payload[COCIS_NET_PAYLOAD_MAX];
    if (cocis_proto_encode(payload, sizeof(payload), &envelope, data) <= 0) {
        return;
    }

    char topic[COCIS_NET_TOPIC_MAX];
    if (cocis_proto_topic_room(topic, sizeof(topic), CONFIG_COCIS_GROUP_PREFIX,
                                room, "cmd")) {
        cocis_net_publish(&app->net, topic, payload, 1, false);
    }

    hub_app_audit(app, actor, "LIGHT_CMD", room == 1 ? "R1" : "R2",
                  "", mode, "OK");
}

/* ------------------------------------------------------------------------- */
/* MQTT receive                                                               */
/* ------------------------------------------------------------------------- */

static void net_rx(const char *topic, const char *payload, size_t len, void *ctx)
{
    (void)len;
    hub_app_t *app = (hub_app_t *)ctx;

    cocis_envelope_t envelope;
    if (!cocis_proto_decode(payload, &envelope)) {
        ESP_LOGW(TAG, "bad message on %s", topic);
        return;
    }

    if (cocis_dedupe_seen(&app->dedupe, envelope.id)) {
        return;
    }

    ESP_LOGI(TAG, "rx %s from %s type %s", topic, envelope.src, envelope.type);

    int room = 0;
    if (envelope.src[0] == 'R' && (envelope.src[1] == '1' || envelope.src[1] == '2')) {
        room = envelope.src[1] - '0';
    }

    if (strcmp(envelope.type, "tele") == 0 && room != 0) {
        const char *lux = strstr(payload, "\"lux\":");
        hub_app_note_room(app, room,
                          strstr(payload, "\"online\":true") != NULL,
                          strstr(payload, "\"occupied\":true") != NULL,
                          strstr(payload, "\"lamp\":true") != NULL,
                          lux != NULL ? atoi(lux + 6) : 0);
    } else if (strcmp(envelope.type, "alert") == 0) {
        hub_hal_beep(400);
        if (strstr(payload, "\"kind\":\"intrusion\"") != NULL) {
            hub_app_audit(app, envelope.src, "INTRUSION", "room", "", "", "ALERT");
            ESP_LOGW(TAG, "intrusion reported by %s", envelope.src);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Background tasks                                                           */
/* ------------------------------------------------------------------------- */

static void publish_status(hub_app_t *app, bool online)
{
    char topic[COCIS_NET_TOPIC_MAX];
    if (!cocis_proto_topic_node(topic, sizeof(topic), CONFIG_COCIS_GROUP_PREFIX,
                                "HUB", "status")) {
        return;
    }

    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"src\":\"HUB\",\"online\":%s,\"sd\":%s,\"time\":%s}",
             online ? "true" : "false",
             storage_ready() ? "true" : "false",
             hub_time_valid() ? "true" : "false");
    cocis_net_publish(&app->net, topic, payload, 1, true);
}

static void hub_task(void *arg)
{
    hub_app_t *app = (hub_app_t *)arg;
    uint32_t last_telemetry = 0;
    uint32_t last_sweep = 0;

    while (true) {
        uint32_t now = hub_now_ms();

        /* Door timers. */
        if (cocis_door_fsm_tick(&app->door_entry, now)) {
            hub_hal_set_door(HUB_DOOR_ENTRY, false);
            hub_hal_leds_off(HUB_DOOR_ENTRY);
        }
        if (cocis_door_fsm_tick(&app->door_exit, now)) {
            hub_hal_set_door(HUB_DOOR_EXIT, false);
            hub_hal_leds_off(HUB_DOOR_EXIT);
        }

        /* Emergency button latches in the HAL ISR. */
        if (hub_hal_take_emergency_press()) {
            hub_app_set_emergency(app, true, "BUTTON");
        }

        /* Tamper switch, rate limited. */
        if (hub_hal_take_tamper_event() && cocis_alerts_tamper_allowed(&app->alerts, now)) {
            hub_app_audit(app, "system", "TAMPER", "enclosure", "", "", "ALERT");
            hub_app_publish_event(app, "alert", "{\"kind\":\"tamper\"}");
            hub_hal_beep(500);
        }

        /* Keep both doors released for as long as emergency mode lasts. */
        if (app->emergency.active) {
            hub_hal_set_door(HUB_DOOR_ENTRY, true);
            hub_hal_set_door(HUB_DOOR_EXIT, true);
        }

        /* When opening hours end, report anybody still inside. */
        if (now - last_sweep >= 1000u) {
            last_sweep = now;

            int year = 0, month = 0, day = 0, hour = 0, minute = 0;
            bool valid = cocis_time_now_eat(&year, &month, &day, &hour,
                                            &minute, NULL);
            bool open = valid &&
                        cocis_schedule_eval(&app->schedule, true, year, month,
                                            day, hour, minute) == COCIS_SCHED_OPEN;

            if (valid) {
                if (app->schedule_known && app->schedule_open && !open) {
                    size_t count = cocis_occupancy_count(&app->occupancy);
                    if (count > 0) {
                        cocis_roll_call_capture(&app->roll_call, &app->occupancy,
                                                (uint32_t)hub_now_utc());
                        char alert[96];
                        snprintf(alert, sizeof(alert),
                                 "{\"kind\":\"closing_sweep\",\"count\":%u}",
                                 (unsigned)count);
                        hub_app_publish_event(app, "alert", alert);
                        hub_app_audit(app, "system", "CLOSING_SWEEP", "building",
                                      "", "", "ALERT");
                        hub_hal_beep(700);

                        for (size_t i = 0; i < cocis_roll_call_count(&app->roll_call); i++) {
                            const cocis_occupant_t *person =
                                cocis_roll_call_at(&app->roll_call, i);
                            ESP_LOGW(TAG, "closing sweep: %s still inside", person->uid);
                        }
                    }
                }
                app->schedule_known = true;
                app->schedule_open = open;
            } else {
                app->schedule_known = false;
            }
        }

        if (now - last_telemetry >= COCIS_HEARTBEAT_S * 1000u) {
            last_telemetry = now;
            publish_status(app, cocis_net_connected(&app->net));

            char telemetry[256];
            char topic[COCIS_NET_TOPIC_MAX];
            snprintf(telemetry, sizeof(telemetry),
                     "{\"src\":\"HUB\",\"wifi\":%s,\"occupancy\":%u,"
                     "\"emergency\":%s,\"sched_ver\":%lu,\"sd\":%s}",
                     cocis_net_connected(&app->net) ? "true" : "false",
                     (unsigned)cocis_occupancy_count(&app->occupancy),
                     app->emergency.active ? "true" : "false",
                     (unsigned long)app->schedule.version,
                     storage_ready() ? "true" : "false");
            if (cocis_proto_topic_node(topic, sizeof(topic), CONFIG_COCIS_GROUP_PREFIX,
                                        "HUB", "tele")) {
                cocis_net_publish(&app->net, topic, telemetry, 1, false);
            }

            char line2[24];
            char line3[24];
            char line4[24];
            snprintf(line2, sizeof(line2), "MODE %s",
                     app->emergency.active ? "EMERGENCY" : "NORMAL");
            snprintf(line3, sizeof(line3), "IN %u  NET %s",
                     (unsigned)cocis_occupancy_count(&app->occupancy),
                     cocis_net_connected(&app->net) ? "OK" : "DOWN");
            snprintf(line4, sizeof(line4), "SD %s",
                     storage_ready() ? "OK" : "FAIL");
            hub_hal_display_show("COCIS HUB", line2, line3, line4);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ------------------------------------------------------------------------- */
/* Startup                                                                    */
/* ------------------------------------------------------------------------- */

hub_app_t *hub_app_instance(void)
{
    return &s_app;
}

void hub_app_start(void)
{
    hub_app_t *app = &s_app;
    memset(app, 0, sizeof(*app));

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    if (storage_init() != ESP_OK) {
        ESP_LOGW(TAG, "SD card unavailable, logging to RAM only");
    }

    ESP_ERROR_CHECK(hub_hal_init());
    cocis_log_store_init(&app->logs);
    cocis_schedule_default(&app->schedule);
    state_store_load_schedule(&app->schedule);
    cocis_occupancy_init(&app->occupancy);
    state_store_load_occupancy(&app->occupancy);
    cocis_alerts_init(&app->alerts);
    cocis_emergency_init(&app->emergency);
    cocis_scan_filter_init(&app->scans);
    cocis_door_fsm_init(&app->door_entry, COCIS_DOOR_OPEN_MS);
    cocis_door_fsm_init(&app->door_exit, COCIS_DOOR_OPEN_MS);
    cocis_dedupe_init(&app->dedupe);
    cocis_event_id_init(&app->ids, "HUB", (uint32_t)(esp_random() % 10000u));

    load_users(app);

    if (state_store_load_auth(&app->auth) != ESP_OK) {
        provision_auth(app);
    }

    err = cocis_time_sntp_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(err));
    }

    char lwt_topic[COCIS_NET_TOPIC_MAX];
    cocis_proto_topic_node(lwt_topic, sizeof(lwt_topic), CONFIG_COCIS_GROUP_PREFIX,
                           "HUB", "status");
    cocis_net_config_t net_config = {
        .src = "HUB",
        .lwt_topic = lwt_topic,
        .lwt_payload = "{\"src\":\"HUB\",\"online\":false}",
    };
    ESP_ERROR_CHECK(cocis_net_start(&app->net, &net_config));
    cocis_net_set_rx(&app->net, net_rx, app);

    char room_topic[COCIS_NET_TOPIC_MAX];
    if (cocis_proto_topic_join(room_topic, sizeof(room_topic),
                                CONFIG_COCIS_GROUP_PREFIX, "room/+/tele")) {
        cocis_net_subscribe(&app->net, room_topic);
    }

    if (cocis_proto_topic_join(room_topic, sizeof(room_topic),
                                CONFIG_COCIS_GROUP_PREFIX, "room/+/event")) {
        cocis_net_subscribe(&app->net, room_topic);
    }

    publish_status(app, true);
    hub_app_publish_schedule(app);
    hub_app_audit(app, "system", "BOOT", "HUB", "", "", "OK");
    hub_hal_display_show("COCIS HUB", "MODE NORMAL", "IN 0  NET BOOT", "SD CHECK");

    xTaskCreate(hub_task, "hub_task", 4096, app, 5, NULL);
    cli_start(app);

    ESP_LOGI(TAG, "entrance hub ready");
}
