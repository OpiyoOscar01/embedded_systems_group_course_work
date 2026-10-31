/**
 * @file cocis_net.c
 * @brief Wi-Fi/MQTT transport implementation.
 * @author Opiyo Oscar
 * @requirements FR-SYN-01..07, FR-DST-02
 */

#include "cocis_net.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

static const char *TAG = "cocis_net";

#define WIFI_BIT BIT0
#define MQTT_BIT BIT1

static uint32_t s_last_link_warn_ms;

static uint32_t link_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------------- */
/* Queue and subscription helpers (all called with `net->lock` held)          */
/* ------------------------------------------------------------------------- */

static void enqueue_locked(cocis_net_t *net, const char *topic, const char *payload,
                           int qos, bool retain)
{
    if (net->queue_count >= COCIS_NET_QUEUE_LEN) {
        /* Drop the oldest so fresh telemetry still gets through. */
        memmove(&net->queue[0], &net->queue[1],
                sizeof(net->queue[0]) * (COCIS_NET_QUEUE_LEN - 1));
        net->queue_count = COCIS_NET_QUEUE_LEN - 1;
        ESP_LOGW(TAG, "offline queue full, dropped oldest message");
    }

    cocis_net_message_t *slot = &net->queue[net->queue_count++];
    snprintf(slot->topic, sizeof(slot->topic), "%s", topic);
    snprintf(slot->payload, sizeof(slot->payload), "%s", payload);
    slot->qos = qos;
    slot->retain = retain;
}

/** Publish or queue one message. */
static int send_message(cocis_net_t *net, const char *topic, const char *payload,
                        int qos, bool retain)
{
    if (topic == NULL || payload == NULL) {
        return -1;
    }

    if (net->mqtt_connected) {
        return esp_mqtt_client_publish(net->client, topic, payload,
                                       (int)strlen(payload), qos, retain ? 1 : 0);
    }

    if (qos <= 0 || retain) {
        return -1;   /* best-effort messages are simply dropped while offline */
    }

    xSemaphoreTake(net->lock, portMAX_DELAY);
    enqueue_locked(net, topic, payload, qos, retain);
    xSemaphoreGive(net->lock);
    return 0;
}

/** Flush queued messages after a reconnect. */
static void flush_queue(cocis_net_t *net)
{
    while (true) {
        cocis_net_message_t message;
        bool have = false;

        xSemaphoreTake(net->lock, portMAX_DELAY);
        if (net->queue_count > 0) {
            message = net->queue[0];
            memmove(&net->queue[0], &net->queue[1],
                    sizeof(net->queue[0]) * (net->queue_count - 1));
            net->queue_count--;
            have = true;
        }
        xSemaphoreGive(net->lock);

        if (!have) {
            return;
        }

        esp_mqtt_client_publish(net->client, message.topic, message.payload,
                                (int)strlen(message.payload), message.qos,
                                message.retain ? 1 : 0);
    }
}

/* ------------------------------------------------------------------------- */
/* MQTT events                                                                */
/* ------------------------------------------------------------------------- */

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                               void *event_data)
{
    (void)base;
    cocis_net_t *net = (cocis_net_t *)arg;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        net->mqtt_connected = true;
        xEventGroupSetBits(net->events, MQTT_BIT);
        ESP_LOGI(TAG, "Broker connected; live updates are flowing.");

        xSemaphoreTake(net->lock, portMAX_DELAY);
        for (size_t i = 0; i < net->subscription_count; i++) {
            esp_mqtt_client_subscribe(net->client, net->subscriptions[i], 1);
        }
        xSemaphoreGive(net->lock);

        flush_queue(net);
        break;

    case MQTT_EVENT_DISCONNECTED:
        net->mqtt_connected = false;
        xEventGroupClearBits(net->events, MQTT_BIT);
        if (link_now_ms() - s_last_link_warn_ms >= 30000u) {
            s_last_link_warn_ms = link_now_ms();
            ESP_LOGW(TAG, "Broker link lost; reconnecting automatically.");
        }
        break;

    case MQTT_EVENT_DATA: {
        if (event->topic == NULL || event->data == NULL || net->rx == NULL) {
            break;
        }

        char topic[COCIS_NET_TOPIC_MAX];
        char payload[COCIS_NET_PAYLOAD_MAX];

        size_t topic_len = (size_t)event->topic_len;
        size_t data_len = (size_t)event->data_len;
        if (topic_len >= sizeof(topic) || data_len >= sizeof(payload)) {
            ESP_LOGW(TAG, "oversized message dropped (%u/%u bytes)",
                     (unsigned)topic_len, (unsigned)data_len);
            break;
        }

        memcpy(topic, event->topic, topic_len);
        topic[topic_len] = '\0';
        memcpy(payload, event->data, data_len);
        payload[data_len] = '\0';

        net->rx(topic, payload, data_len, net->rx_ctx);
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGD(TAG, "broker transport error (type %d)", event->error_handle != NULL
                                                          ? event->error_handle->error_type
                                                          : -1);
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Wi-Fi events                                                               */
/* ------------------------------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                               void *event_data)
{
    cocis_net_t *net = (cocis_net_t *)arg;

    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        net->wifi_connected = false;
        xEventGroupClearBits(net->events, WIFI_BIT);
        ESP_LOGD(TAG, "Wi-Fi link lost; reconnecting automatically.");
        esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        net->wifi_connected = true;
        xEventGroupSetBits(net->events, WIFI_BIT);
        ESP_LOGI(TAG, "Wi-Fi connected, ip " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

esp_err_t cocis_net_start(cocis_net_t *net, const cocis_net_config_t *config)
{
    if (net == NULL || config == NULL || config->src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(net, 0, sizeof(*net));

    net->events = xEventGroupCreate();
    net->lock = xSemaphoreCreateMutex();
    if (net->events == NULL || net->lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    snprintf(net->client_id, sizeof(net->client_id), "%s-%s-%08lx",
             CONFIG_COCIS_GROUP_PREFIX, config->src,
             (unsigned long)esp_random());

    if (config->lwt_topic != NULL) {
        snprintf(net->lwt_topic, sizeof(net->lwt_topic), "%s", config->lwt_topic);
        snprintf(net->lwt_payload, sizeof(net->lwt_payload),
                 "%s", config->lwt_payload != NULL ? config->lwt_payload : "offline");
    }

    /* Transient broker reconnects are normal on a shared/public broker; hide
     * the low-level transport errors and report state through `net` instead. */
    esp_log_level_set("mqtt_client", ESP_LOG_NONE);
    esp_log_level_set("transport_base", ESP_LOG_NONE);
    esp_log_level_set("esp-tls", ESP_LOG_NONE);

    /* --- Wi-Fi station ------------------------------------------------- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, net, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, net, NULL));

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid),
             "%s", CONFIG_COCIS_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password),
             "%s", CONFIG_COCIS_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode =
        CONFIG_COCIS_WIFI_PASSWORD[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* --- MQTT client --------------------------------------------------- */
    esp_mqtt_client_config_t mqtt_config = {0};
    mqtt_config.broker.address.uri = CONFIG_COCIS_MQTT_URI;
    mqtt_config.credentials.client_id = net->client_id;
    mqtt_config.session.keepalive = 15;
    mqtt_config.session.disable_clean_session = false;
    mqtt_config.network.reconnect_timeout_ms = 2000;

    if (net->lwt_topic[0] != '\0') {
        mqtt_config.session.last_will.topic = net->lwt_topic;
        mqtt_config.session.last_will.msg = net->lwt_payload;
        mqtt_config.session.last_will.msg_len = (int)strlen(net->lwt_payload);
        mqtt_config.session.last_will.qos = 1;
        mqtt_config.session.last_will.retain = 0;
    }

    net->client = esp_mqtt_client_init(&mqtt_config);
    if (net->client == NULL) {
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        net->client, ESP_EVENT_ANY_ID, mqtt_event_handler, net));
    ESP_ERROR_CHECK(esp_mqtt_client_start(net->client));

    ESP_LOGI(TAG, "started, broker %s", CONFIG_COCIS_MQTT_URI);
    return ESP_OK;
}

void cocis_net_set_rx(cocis_net_t *net, cocis_net_rx_cb_t callback, void *ctx)
{
    if (net == NULL) {
        return;
    }
    net->rx = callback;
    net->rx_ctx = ctx;
}

esp_err_t cocis_net_subscribe(cocis_net_t *net, const char *topic)
{
    if (net == NULL || topic == NULL || topic[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(net->lock, portMAX_DELAY);
    if (net->subscription_count >= COCIS_NET_SUB_MAX) {
        xSemaphoreGive(net->lock);
        return ESP_ERR_NO_MEM;
    }
    snprintf(net->subscriptions[net->subscription_count],
             sizeof(net->subscriptions[0]), "%s", topic);
    net->subscription_count++;
    xSemaphoreGive(net->lock);

    if (net->mqtt_connected) {
        int id = esp_mqtt_client_subscribe(net->client, topic, 1);
        if (id < 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

int cocis_net_publish(cocis_net_t *net, const char *topic, const char *payload,
                      int qos, bool retain)
{
    if (net == NULL) {
        return -1;
    }
    return send_message(net, topic, payload, qos, retain);
}

bool cocis_net_connected(const cocis_net_t *net)
{
    return net != NULL && net->wifi_connected && net->mqtt_connected;
}
