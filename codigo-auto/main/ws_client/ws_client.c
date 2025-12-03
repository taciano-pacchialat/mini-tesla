/**
 * @file ws_client.c
 * @brief WebSocket Client implementation using esp_websocket_client
 */

#include "ws_client.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "[WebSocket]";

// WebSocket client handle
static esp_websocket_client_handle_t s_client = NULL;

// Callback for telemetry data
static telemetry_callback_t s_telemetry_callback = NULL;

// Mutex for thread-safe access to last telemetry
static SemaphoreHandle_t s_telemetry_mutex = NULL;
static telemetry_data_t s_last_telemetry = {0};
static bool s_telemetry_valid = false;

// Connection state
static bool s_is_connected = false;

/**
 * @brief Parse JSON telemetry data
 */
static bool parse_telemetry_json(const char *json_str, telemetry_data_t *data)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return false;
    }

    // Parse all fields
    cJSON *item;

    item = cJSON_GetObjectItem(root, "object_type");
    if (item && cJSON_IsString(item))
    {
        strncpy(data->object_type, item->valuestring, sizeof(data->object_type) - 1);
    }

    item = cJSON_GetObjectItem(root, "pixel_x");
    if (item && cJSON_IsNumber(item))
    {
        data->pixel_x = item->valueint;
    }

    item = cJSON_GetObjectItem(root, "pixel_y");
    if (item && cJSON_IsNumber(item))
    {
        data->pixel_y = item->valueint;
    }

    item = cJSON_GetObjectItem(root, "world_x");
    if (item && cJSON_IsNumber(item))
    {
        data->world_x = (float)item->valuedouble;
    }

    item = cJSON_GetObjectItem(root, "world_y");
    if (item && cJSON_IsNumber(item))
    {
        data->world_y = (float)item->valuedouble;
    }

    item = cJSON_GetObjectItem(root, "distance_cm");
    if (item && cJSON_IsNumber(item))
    {
        data->distance_cm = (float)item->valuedouble;
    }

    item = cJSON_GetObjectItem(root, "angle_deg");
    if (item && cJSON_IsNumber(item))
    {
        data->angle_deg = (float)item->valuedouble;
    }

    item = cJSON_GetObjectItem(root, "pixel_count");
    if (item && cJSON_IsNumber(item))
    {
        data->pixel_count = item->valueint;
    }

    item = cJSON_GetObjectItem(root, "detected");
    if (item && cJSON_IsBool(item))
    {
        data->detected = cJSON_IsTrue(item);
    }

    item = cJSON_GetObjectItem(root, "timestamp_ms");
    if (item && cJSON_IsNumber(item))
    {
        data->timestamp_ms = (uint64_t)item->valuedouble;
    }

    cJSON_Delete(root);
    return true;
}

/**
 * @brief WebSocket event handler
 */
static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id)
    {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected to server");
        s_is_connected = true;
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected, will auto-reconnect...");
        s_is_connected = false;
        break;

    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "Received WebSocket data: opcode=%d, len=%d",
                 data->op_code, data->data_len);

        if (data->op_code == 0x01)
        { // Text frame (JSON telemetry)
            // Null-terminate the data
            char *json_str = malloc(data->data_len + 1);
            if (json_str)
            {
                memcpy(json_str, data->data_ptr, data->data_len);
                json_str[data->data_len] = '\0';

                ESP_LOGI(TAG, "Telemetry JSON: %s", json_str);

                telemetry_data_t telemetry;
                if (parse_telemetry_json(json_str, &telemetry))
                {
                    // Store in thread-safe manner
                    if (xSemaphoreTake(s_telemetry_mutex, portMAX_DELAY))
                    {
                        memcpy(&s_last_telemetry, &telemetry, sizeof(telemetry_data_t));
                        s_telemetry_valid = true;
                        xSemaphoreGive(s_telemetry_mutex);
                    }

                    // Call callback if registered
                    if (s_telemetry_callback)
                    {
                        s_telemetry_callback(&telemetry);
                    }
                }

                free(json_str);
            }
        }
        else if (data->op_code == 0x02)
        { // Binary frame (video)
            ESP_LOGD(TAG, "Received video frame: %d bytes", data->data_len);
            // Video frames can be processed here if needed for debugging
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error occurred");
        s_is_connected = false;
        break;

    default:
        break;
    }
}

esp_err_t ws_client_init(telemetry_callback_t callback)
{
    ESP_LOGI(TAG, "Initializing WebSocket client...");

    s_telemetry_callback = callback;

    // Create mutex for telemetry access
    s_telemetry_mutex = xSemaphoreCreateMutex();
    if (s_telemetry_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create telemetry mutex");
        return ESP_FAIL;
    }

    // Configure WebSocket client
    esp_websocket_client_config_t ws_cfg = {
        .uri = WS_SERVER_URI,
        .reconnect_timeout_ms = WS_RECONNECT_TIMEOUT_MS,
        .network_timeout_ms = 10000,
        .buffer_size = WS_MAX_PAYLOAD_SIZE,
    };

    s_client = esp_websocket_client_init(&ws_cfg);
    if (s_client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        return ESP_FAIL;
    }

    // Register event handler
    esp_err_t err = esp_websocket_register_events(s_client,
                                                  WEBSOCKET_EVENT_ANY,
                                                  websocket_event_handler,
                                                  NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register WebSocket event handler");
        return err;
    }

    ESP_LOGI(TAG, "WebSocket client initialized successfully");
    return ESP_OK;
}

esp_err_t ws_client_connect(void)
{
    if (s_client == NULL)
    {
        ESP_LOGE(TAG, "WebSocket client not initialized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to WebSocket server: %s", WS_SERVER_URI);

    esp_err_t err = esp_websocket_client_start(s_client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start WebSocket client");
        return err;
    }

    ESP_LOGI(TAG, "WebSocket client started");
    return ESP_OK;
}

esp_err_t ws_client_send_status(const vehicle_status_t *status)
{
    if (s_client == NULL || !s_is_connected)
    {
        ESP_LOGW(TAG, "Cannot send status: not connected");
        return ESP_FAIL;
    }

    // Create JSON object
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "vehicle_id", status->vehicle_id);

    cJSON *motors = cJSON_CreateObject();
    cJSON_AddNumberToObject(motors, "left", status->motor_left);
    cJSON_AddNumberToObject(motors, "right", status->motor_right);
    cJSON_AddItemToObject(root, "motors", motors);

    cJSON_AddNumberToObject(root, "battery_mv", status->battery_mv);
    cJSON_AddStringToObject(root, "status", status->status);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL)
    {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Sending status: %s", json_str);

    // Send as text frame
    int sent = esp_websocket_client_send_text(s_client, json_str, strlen(json_str), portMAX_DELAY);

    free(json_str);
    cJSON_Delete(root);

    if (sent < 0)
    {
        ESP_LOGE(TAG, "Failed to send WebSocket message");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ws_client_send_frame(const uint8_t *frame, size_t length)
{
    if (frame == NULL || length == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (length > WS_MAX_PAYLOAD_SIZE)
    {
        ESP_LOGW(TAG, "JPEG demasiado grande (%d bytes > %d) - descartado",
                 (int)length, WS_MAX_PAYLOAD_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_client == NULL || !ws_client_is_connected())
    {
        ESP_LOGW(TAG, "Cannot send frame: WebSocket no conectado");
        return ESP_FAIL;
    }

    int sent = esp_websocket_client_send_bin(s_client,
                                             (const char *)frame,
                                             length,
                                             pdMS_TO_TICKS(1000));
    if (sent < 0)
    {
        ESP_LOGE(TAG, "Error enviando frame binario (%d bytes)", (int)length);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Frame binario enviado: %d bytes", sent);
    return ESP_OK;
}

bool ws_client_is_connected(void)
{
    return s_is_connected && (s_client != NULL) &&
           esp_websocket_client_is_connected(s_client);
}

esp_err_t ws_client_disconnect(void)
{
    if (s_client == NULL)
    {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Disconnecting WebSocket client...");
    s_is_connected = false;

    esp_err_t err = esp_websocket_client_stop(s_client);
    if (err == ESP_OK)
    {
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        ESP_LOGI(TAG, "WebSocket client disconnected");
    }

    return err;
}

esp_err_t ws_client_get_last_telemetry(telemetry_data_t *data)
{
    if (data == NULL || !s_telemetry_valid)
    {
        return ESP_FAIL;
    }

    if (xSemaphoreTake(s_telemetry_mutex, pdMS_TO_TICKS(100)))
    {
        memcpy(data, &s_last_telemetry, sizeof(telemetry_data_t));
        xSemaphoreGive(s_telemetry_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}
