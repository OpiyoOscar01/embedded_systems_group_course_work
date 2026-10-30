/**
 * @file state_store.c
 * @brief NVS/SD persistence bridge for schedule and occupancy.
 * @author Opiyo Oscar
 * @requirements FR-SCH-06, FR-OCC-05, NFR-05
 */

#include "state_store.h"

#include <string.h>

#include "nvs.h"
#include "occupancy.h"
#include "storage.h"

#define SCHED_NAMESPACE  "sched"
#define SCHED_KEY_JSON   "json_blob"

#define OCC_NAMESPACE    "occ"
#define OCC_KEY_TABLE    "table"

#define AUTH_NAMESPACE   "auth"
#define AUTH_KEY_USERS   "credentials"

/* Only the static credential table is persisted; the live session is not. */
typedef struct {
    cocis_credential_t users[COCIS_AUTH_MAX_USERS];
    size_t count;
} auth_blob_t;

#define SCHED_JSON_MAX   1024

esp_err_t state_store_load_schedule(cocis_schedule_t *schedule)
{
    if (schedule == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cocis_schedule_default(schedule);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SCHED_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char json[SCHED_JSON_MAX];
    size_t length = sizeof(json) - 1;
    err = nvs_get_blob(handle, SCHED_KEY_JSON, json, &length);
    nvs_close(handle);

    if (err != ESP_OK) {
        return err;
    }
    json[length] = '\0';

    if (!cocis_schedule_from_json(json, schedule)) {
        cocis_schedule_default(schedule);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t state_store_save_schedule(const cocis_schedule_t *schedule)
{
    if (schedule == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char json[SCHED_JSON_MAX];
    int length = cocis_schedule_to_json(schedule, json, sizeof(json));
    if (length <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SCHED_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, SCHED_KEY_JSON, json, (size_t)length + 1);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    /* Human-readable mirror; absence of an SD card is not an error. */
    if (storage_ready()) {
        storage_write("config/schedule.json", json);
    }
    return err;
}

esp_err_t state_store_load_occupancy(cocis_occupancy_t *occupancy)
{
    if (occupancy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cocis_occupancy_init(occupancy);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(OCC_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    cocis_occupancy_t stored;
    size_t length = sizeof(stored);
    err = nvs_get_blob(handle, OCC_KEY_TABLE, &stored, &length);
    nvs_close(handle);

    if (err != ESP_OK || length != sizeof(stored) || stored.count > COCIS_OCCUPANCY_MAX) {
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }

    /* Reject garbage from a partially written blob. */
    for (size_t i = 0; i < stored.count; i++) {
        if (stored.inside[i].uid[0] == '\0' ||
            stored.inside[i].uid[COCIS_UID_MAX - 1] != '\0') {
            return ESP_ERR_INVALID_STATE;
        }
    }

    *occupancy = stored;
    return ESP_OK;
}

esp_err_t state_store_save_occupancy(const cocis_occupancy_t *occupancy)
{
    if (occupancy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(OCC_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, OCC_KEY_TABLE, occupancy, sizeof(*occupancy));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t state_store_load_auth(cocis_auth_t *auth)
{
    if (auth == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cocis_auth_init(auth);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(AUTH_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    auth_blob_t blob;
    size_t length = sizeof(blob);
    err = nvs_get_blob(handle, AUTH_KEY_USERS, &blob, &length);
    nvs_close(handle);

    if (err != ESP_OK || length != sizeof(blob) ||
        blob.count == 0 || blob.count > COCIS_AUTH_MAX_USERS) {
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }

    for (size_t i = 0; i < blob.count; i++) {
        if (!cocis_auth_add(auth, blob.users[i].username, blob.users[i].role,
                            blob.users[i].salt, blob.users[i].hash,
                            blob.users[i].must_change)) {
            cocis_auth_init(auth);
            return ESP_ERR_INVALID_STATE;
        }
        /* Preserve the lockout counters as well. */
        auth->users[auth->count - 1].fails = blob.users[i].fails;
        auth->users[auth->count - 1].locked_until_ms = blob.users[i].locked_until_ms;
    }
    auth->session_user = -1;
    return ESP_OK;
}

esp_err_t state_store_save_auth(const cocis_auth_t *auth)
{
    if (auth == NULL || auth->count > COCIS_AUTH_MAX_USERS) {
        return ESP_ERR_INVALID_ARG;
    }

    auth_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.count = auth->count;
    memcpy(blob.users, auth->users, sizeof(auth->users[0]) * auth->count);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(AUTH_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, AUTH_KEY_USERS, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
