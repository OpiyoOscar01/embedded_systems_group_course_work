/**
 * @file app.c
 * @brief Room automation: PIR + LDR -> lamp, energy accounting, telemetry.
 * @author Opiyo Oscar
 * @requirements FR-LGT-01..09, FR-SYN-01..07, FR-LGT-06
 */

#include "app.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cocis_proto.h"
#include "cocis_time.h"
#include "cocis_time_sntp.h"
#include "cocis_types.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "room_hal.h"
#include "sdkconfig.h"
#include "storage.h"
#include "cli.h"

static const char *TAG = "room_app";
static room_app_t s_app;

uint32_t room_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool room_time_valid(void)
{
    int year = 0;
    return cocis_time_now_eat(&year, NULL, NULL, NULL, NULL, NULL) &&
           year >= COCIS_TIME_MIN_VALID_YEAR;
}

/* ------------------------------------------------------------------------- */
/* Energy baseline                                                            */
/* ------------------------------------------------------------------------- */

/**
 * Without automation the lamp would follow opening hours (Mon-Fri 07-21,
 * Sat 08-14). This is the reference used for the saving estimate.
 */
bool room_app_is_open(const room_app_t *app)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if (app == NULL ||
        !cocis_time_now_eat(&year, &month, &day, &hour, &minute, NULL)) {
        return false;
    }

    return cocis_schedule_eval(&app->schedule, true, year, month, day,
                               hour, minute) == COCIS_SCHED_OPEN;
}

/** Without automation the lamp would follow the distributed schedule. */
static bool baseline_expected(const room_app_t *app)
{
    return room_app_is_open(app);
}

/* ------------------------------------------------------------------------- */
/* Schedule persistence and event helpers                                     */
/* ------------------------------------------------------------------------- */

/** Load the last schedule the hub sent; the default is used on first boot. */
static void load_schedule(room_app_t *app)
{
    cocis_schedule_default(&app->schedule);

    nvs_handle_t handle;
    if (nvs_open("sched", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    char json[1024];
    size_t length = sizeof(json) - 1;
    if (nvs_get_blob(handle, "json_blob", json, &length) == ESP_OK) {
        json[length] = '\0';
        cocis_schedule_from_json(json, &app->schedule);
    }
    nvs_close(handle);
}

static void save_schedule(const room_app_t *app)
{
    char json[1024];
    int length = cocis_schedule_to_json(&app->schedule, json, sizeof(json));
    if (length <= 0) {
        return;
    }

    nvs_handle_t handle;
    if (nvs_open("sched", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_blob(handle, "json_blob", json, (size_t)length + 1);
    nvs_commit(handle);
    nvs_close(handle);
}

/** Publish one event envelope on this room's event topic. */
static void publish_event(room_app_t *app, const char *type, const char *data)
{
    cocis_envelope_t envelope = {0};
    envelope.version = COCIS_PROTO_VERSION;
    envelope.ts = (uint64_t)time(NULL);
    cocis_event_id_next(&app->ids, envelope.id, sizeof(envelope.id));
    snprintf(envelope.src, sizeof(envelope.src), "%s", app->node);
    snprintf(envelope.type, sizeof(envelope.type), "%s", type);

    char payload[COCIS_NET_PAYLOAD_MAX];
    if (cocis_proto_encode(payload, sizeof(payload), &envelope, data) <= 0) {
        return;
    }

    char topic[COCIS_NET_TOPIC_MAX];
    if (cocis_proto_topic_room(topic, sizeof(topic), CONFIG_COCIS_GROUP_PREFIX,
                                CONFIG_COCIS_ROOM_ID, "event")) {
        cocis_net_publish(&app->net, topic, payload, 1, false);
    }
}

/* ------------------------------------------------------------------------- */
/* Telemetry                                                                  */
/* ------------------------------------------------------------------------- */

static void publish_status(room_app_t *app, bool online)
{
    char topic[COCIS_NET_TOPIC_MAX];
    if (!cocis_proto_topic_node(topic, sizeof(topic), CONFIG_COCIS_GROUP_PREFIX,
                                app->node, "status")) {
        return;
    }

    char payload[128];
    snprintf(payload, sizeof(payload), "{\"src\":\"%s\",\"online\":%s}",
             app->node, online ? "true" : "false");
    cocis_net_publish(&app->net, topic, payload, 1, true);
}

static void publish_telemetry(room_app_t *app)
{
    char topic[COCIS_NET_TOPIC_MAX];
    if (!cocis_proto_topic_room(topic, sizeof(topic), CONFIG_COCIS_GROUP_PREFIX,
                                CONFIG_COCIS_ROOM_ID, "tele")) {
        return;
    }

    cocis_envelope_t envelope = {0};
    envelope.version = COCIS_PROTO_VERSION;
    envelope.ts = (uint64_t)time(NULL);
    cocis_event_id_next(&app->ids, envelope.id, sizeof(envelope.id));
    snprintf(envelope.src, sizeof(envelope.src), "%s", app->node);
    snprintf(envelope.type, sizeof(envelope.type), "tele");

    char data[300];
    snprintf(data, sizeof(data),
             "{\"room\":%d,\"occupied\":%s,\"lamp\":%s,\"lux\":%d,"
             "\"state\":%d,\"override\":%u,\"used_mwh\":%llu,\"saved_mwh\":%llu,"
             "\"sched_ver\":%lu,\"online\":%s}",
             CONFIG_COCIS_ROOM_ID,
             app->light.occupied ? "true" : "false",
             app->last_relay ? "true" : "false",
             app->sim_lux_active ? app->sim_lux : room_hal_read_lux(),
             (int)app->light.state,
             (unsigned)(cocis_light_override_remaining_ms(&app->light, room_now_ms()) / 1000u),
             (unsigned long long)cocis_energy_mwh_used(&app->energy, COCIS_LAMP_W),
             (unsigned long long)cocis_energy_mwh_saved(&app->energy, COCIS_LAMP_W),
             (unsigned long)app->schedule.version,
             cocis_net_connected(&app->net) ? "true" : "false");

    char payload[COCIS_NET_PAYLOAD_MAX];
    if (cocis_proto_encode(payload, sizeof(payload), &envelope, data) > 0) {
        cocis_net_publish(&app->net, topic, payload, 1, false);
    }
}

/** Append a one-minute energy sample to the daily CSV file. */
static void log_energy(room_app_t *app)
{
    if (!storage_ready()) {
        return;
    }

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);

    char date[9];
    char stamp[20];
    strftime(date, sizeof(date), "%Y%m%d", &local);
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &local);

    char line[160];
    snprintf(line, sizeof(line), "%s,R%d,%llu,%llu,%llu,%llu",
             stamp, CONFIG_COCIS_ROOM_ID,
             (unsigned long long)app->energy.lamp_on_s,
             (unsigned long long)app->energy.baseline_s,
             (unsigned long long)cocis_energy_mwh_used(&app->energy, COCIS_LAMP_W),
             (unsigned long long)cocis_energy_mwh_saved(&app->energy, COCIS_LAMP_W));

    char path[64];
    snprintf(path, sizeof(path), "logs/energy/%s.csv", date);
    storage_append(path, line);
}

/* ------------------------------------------------------------------------- */
/* MQTT receive                                                               */
/* ------------------------------------------------------------------------- */

static void net_rx(const char *topic, const char *payload, size_t len, void *ctx)
{
    (void)len;
    room_app_t *app = (room_app_t *)ctx;

    /* Retained schedule from the hub is a raw schedule document. */
    if (strstr(topic, "/schedule") != NULL) {
        cocis_schedule_t incoming;
        if (cocis_schedule_from_json(payload, &incoming)) {
            if (incoming.version >= app->schedule.version) {
                app->schedule = incoming;
                save_schedule(app);
                ESP_LOGI(TAG, "schedule v%lu applied",
                         (unsigned long)app->schedule.version);
            }
        } else {
            ESP_LOGW(TAG, "bad schedule document");
        }
        return;
    }

    cocis_envelope_t envelope;
    if (!cocis_proto_decode(payload, &envelope)) {
        ESP_LOGW(TAG, "bad message on %s", topic);
        return;
    }

    if (strcmp(envelope.type, "cmd") != 0) {
        return;
    }

    if (strstr(payload, "\"light\":\"on\"") != NULL ||
        strstr(payload, "\"override\":true") != NULL) {
        room_app_set_light(app, "on");
    } else if (strstr(payload, "\"light\":\"off\"") != NULL) {
        room_app_set_light(app, "off");
    } else if (strstr(payload, "\"light\":\"auto\"") != NULL) {
        room_app_set_light(app, "auto");
    }
    ESP_LOGI(TAG, "command from %s: %.*s", envelope.src,
             (int)envelope.data_len, envelope.data);
}

/* ------------------------------------------------------------------------- */
/* Control task                                                               */
/* ------------------------------------------------------------------------- */

static void room_task(void *arg)
{
    room_app_t *app = (room_app_t *)arg;
    uint32_t last_telemetry = 0;
    uint32_t last_energy_tick = room_now_ms();

    while (true) {
        uint32_t now = room_now_ms();

        if (room_hal_take_override_press()) {
            room_app_override(app, 0);
            ESP_LOGI(TAG, "local override");
        }

        bool motion = app->sim_motion_active ? app->sim_motion_value
                                             : room_hal_read_motion();
        int lux = app->sim_lux_active ? app->sim_lux : room_hal_read_lux();

        /* Motion while the building is closed is an intrusion signal. */
        if (motion && room_time_valid() && !room_app_is_open(app) &&
            (now - app->last_intrusion_ms) >= 30000u) {
            app->last_intrusion_ms = now;
            char alert[80];
            snprintf(alert, sizeof(alert),
                     "{\"kind\":\"intrusion\",\"room\":%d}", CONFIG_COCIS_ROOM_ID);
            publish_event(app, "alert", alert);
            ESP_LOGW(TAG, "intrusion: motion outside opening hours");
        }

        /* LDR hysteresis: enough light above LUX_OFF, dark below LUX_ON,
         * keep the previous decision in between. */
        bool daylight = app->light.daylight;
        if (lux >= COCIS_LUX_OFF) {
            daylight = true;
        } else if (lux < COCIS_LUX_ON) {
            daylight = false;
        }

        /* Outside opening hours occupancy is reported but lamps stay off. */
        bool within_hours = !room_time_valid() || room_app_is_open(app);
        if (!motion) {
            app->light_forced_off = false;
        }
        bool light_motion = motion && within_hours && !app->light_forced_off;
        bool relay = cocis_light_update(&app->light, now, light_motion, daylight);
        if (relay != app->last_relay) {
            room_hal_set_relay(relay);
            app->last_relay = relay;
        }

        room_hal_set_led_occupied(app->light.occupied);
        room_hal_set_led_lights(relay);
        room_hal_set_led_network(cocis_net_connected(&app->net));

        /* Integrate energy once per second. */
        uint32_t elapsed = now - last_energy_tick;
        if (elapsed >= 1000u) {
            uint32_t seconds = elapsed / 1000u;
            cocis_energy_add(&app->energy, seconds, relay, baseline_expected(app));
            last_energy_tick += seconds * 1000u;
        }

        if (now - last_telemetry >= COCIS_HEARTBEAT_S * 1000u) {
            last_telemetry = now;
            publish_status(app, cocis_net_connected(&app->net));
            publish_telemetry(app);
        }

        if (now - app->last_energy_log_ms >= 60000u) {
            app->last_energy_log_ms = now;
            log_energy(app);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

room_app_t *room_app_instance(void)
{
    return &s_app;
}

void room_app_override(room_app_t *app, uint32_t minutes)
{
    uint32_t duration_ms = minutes == 0 ? 0 : minutes * 60000u;
    cocis_light_override(&app->light, room_now_ms(), duration_ms);
}

void room_app_set_light(room_app_t *app, const char *mode)
{
    if (app == NULL || mode == NULL) {
        return;
    }

    if (strcmp(mode, "on") == 0) {
        app->light_forced_off = false;
        room_app_override(app, 0);
    } else if (strcmp(mode, "off") == 0) {
        app->light_forced_off = true;
        cocis_light_force_off(&app->light);
        room_hal_set_relay(false);
        app->last_relay = false;
    } else if (strcmp(mode, "auto") == 0) {
        app->light_forced_off = false;
        cocis_light_cancel_override(&app->light);
    }
}

void room_app_sim_motion(room_app_t *app, bool active, bool value)
{
    if (app == NULL) {
        return;
    }
    app->sim_motion_active = active;
    app->sim_motion_value = value;
}

void room_app_sim_lux(room_app_t *app, bool active, int value)
{
    if (app == NULL) {
        return;
    }
    app->sim_lux_active = active;
    app->sim_lux = value;
}

void room_app_start(void)
{
    room_app_t *app = &s_app;
    memset(app, 0, sizeof(*app));
    snprintf(app->node, sizeof(app->node), "R%d", CONFIG_COCIS_ROOM_ID);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    if (storage_init() != ESP_OK) {
        ESP_LOGW(TAG, "SD card unavailable, energy log disabled");
    }

    ESP_ERROR_CHECK(room_hal_init());
    cocis_light_init(&app->light, NULL);
    cocis_energy_init(&app->energy);
    load_schedule(app);
    cocis_event_id_init(&app->ids, app->node, (uint32_t)(esp_random() % 10000u));
    app->last_relay = false;
    app->last_energy_ms = room_now_ms();
    app->last_energy_log_ms = room_now_ms();
    room_hal_set_relay(false);

    char lwt_topic[COCIS_NET_TOPIC_MAX];
    cocis_proto_topic_node(lwt_topic, sizeof(lwt_topic), CONFIG_COCIS_GROUP_PREFIX,
                           app->node, "status");
    char lwt_payload[64];
    snprintf(lwt_payload, sizeof(lwt_payload), "{\"src\":\"%s\",\"online\":false}", app->node);

    cocis_net_config_t net_config = {
        .src = app->node,
        .lwt_topic = lwt_topic,
        .lwt_payload = lwt_payload,
    };
    ESP_ERROR_CHECK(cocis_net_start(&app->net, &net_config));
    cocis_net_set_rx(&app->net, net_rx, app);

    /* SNTP uses the lwIP timers, so it must start after the network stack. */
    cocis_time_sntp_start();

    char cmd_topic[COCIS_NET_TOPIC_MAX];
    if (cocis_proto_topic_room(cmd_topic, sizeof(cmd_topic), CONFIG_COCIS_GROUP_PREFIX,
                                CONFIG_COCIS_ROOM_ID, "cmd")) {
        cocis_net_subscribe(&app->net, cmd_topic);
    }

    char schedule_topic[COCIS_NET_TOPIC_MAX];
    if (cocis_proto_topic_node(schedule_topic, sizeof(schedule_topic),
                                CONFIG_COCIS_GROUP_PREFIX, "HUB", "schedule")) {
        cocis_net_subscribe(&app->net, schedule_topic);
    }

    publish_status(app, true);
    xTaskCreate(room_task, "room_task", 4096, app, 5, NULL);
    cli_start(app);

    ESP_LOGI(TAG, "room %d ready", CONFIG_COCIS_ROOM_ID);
}
