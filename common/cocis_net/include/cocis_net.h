/**
 * @file cocis_net.h
 * @brief Wi-Fi STA + MQTT transport shared by every terminal.
 *
 * One instance per firmware. The component owns Wi-Fi association, the MQTT
 * client, keepalive/LWT, topic subscriptions and a small store-and-forward
 * queue so telemetry produced while offline is published after reconnecting
 * (FR-SYN-06, NFR-06).
 *
 * The application receives complete messages through a single callback and
 * dispatches on the topic; payloads are copied and NUL terminated first.
 *
 * @author Opiyo Oscar
 * @requirements FR-SYN-01..07, FR-DST-02
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"

#define COCIS_NET_TOPIC_MAX    96
#define COCIS_NET_PAYLOAD_MAX  512
#define COCIS_NET_QUEUE_LEN    8
#define COCIS_NET_SUB_MAX      8
#define COCIS_NET_CLIENT_ID_MAX 64

typedef void (*cocis_net_rx_cb_t)(const char *topic, const char *payload,
                                  size_t len, void *ctx);

typedef struct {
    const char *src;          /* "HUB", "R1", "R2" */
    const char *lwt_topic;    /* NULL disables the last will */
    const char *lwt_payload;
} cocis_net_config_t;

typedef struct {
    char topic[COCIS_NET_TOPIC_MAX];
    char payload[COCIS_NET_PAYLOAD_MAX];
    int qos;
    bool retain;
} cocis_net_message_t;

typedef struct {
    esp_mqtt_client_handle_t client;
    EventGroupHandle_t events;
    SemaphoreHandle_t lock;

    volatile bool wifi_connected;
    volatile bool mqtt_connected;

    cocis_net_rx_cb_t rx;
    void *rx_ctx;

    char subscriptions[COCIS_NET_SUB_MAX][COCIS_NET_TOPIC_MAX];
    size_t subscription_count;

    cocis_net_message_t queue[COCIS_NET_QUEUE_LEN];
    size_t queue_count;

    char client_id[COCIS_NET_CLIENT_ID_MAX];
    char lwt_topic[COCIS_NET_TOPIC_MAX];
    char lwt_payload[COCIS_NET_PAYLOAD_MAX];
} cocis_net_t;

/** Start Wi-Fi and the MQTT client. Call after nvs_flash_init(). */
esp_err_t cocis_net_start(cocis_net_t *net, const cocis_net_config_t *config);

/** Register the single message callback (topic is a normal C string). */
void cocis_net_set_rx(cocis_net_t *net, cocis_net_rx_cb_t callback, void *ctx);

/**
 * Subscribe to a topic. Safe to call before the broker connects; the request
 * is replayed on every (re)connection.
 */
esp_err_t cocis_net_subscribe(cocis_net_t *net, const char *topic);

/**
 * Publish a message.
 *
 * When the broker is down, QoS>0 non-retained messages are queued (up to
 * COCIS_NET_QUEUE_LEN) and flushed after reconnect. Returns the MQTT message
 * id, 0 when queued, or -1 on error.
 */
int cocis_net_publish(cocis_net_t *net, const char *topic, const char *payload,
                      int qos, bool retain);

/** True when both Wi-Fi and MQTT are up. */
bool cocis_net_connected(const cocis_net_t *net);
