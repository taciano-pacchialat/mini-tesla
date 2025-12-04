/**
 * @file ws_client.c
 * @brief WebSocket Client implementation using esp_websocket_client
 */

#include "ws_client.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <inttypes.h>
#include <stdlib.h>

static const char *TAG = "[WebSocket]";

// WebSocket client handle
static esp_websocket_client_handle_t s_client = NULL;

// Callback for manual control commands
static control_callback_t s_control_callback = NULL;
static char s_vehicle_id[32] = {0};

// Connection state
static bool s_is_connected = false;
static volatile bool s_stream_enabled = false;

static control_command_t control_command_from_string(const char *command)
{
    if (!command)
    {
        return CONTROL_CMD_STOP;
    }

    if (strcmp(command, "forward") == 0)
    {
        return CONTROL_CMD_FORWARD;
    }
    if (strcmp(command, "backward") == 0)
    {
        return CONTROL_CMD_BACKWARD;
    }
    if (strcmp(command, "left") == 0)
    {
        return CONTROL_CMD_LEFT;
    }
    if (strcmp(command, "right") == 0)
    {
        return CONTROL_CMD_RIGHT;
    }

    return CONTROL_CMD_STOP;
}

static void handle_stream_status_message(const cJSON *root)
{
    bool enable = false;
    int viewer_count = 0;

    const cJSON *enable_item = cJSON_GetObjectItem(root, "enable");
    const cJSON *viewer_item = cJSON_GetObjectItem(root, "viewer_count");

    if (enable_item)
    {
        if (cJSON_IsBool(enable_item))
        {
            enable = cJSON_IsTrue(enable_item);
        }
        else if (cJSON_IsNumber(enable_item))
        {
            enable = (enable_item->valuedouble != 0);
        }
    }

    if (viewer_item && cJSON_IsNumber(viewer_item))
    {
        viewer_count = viewer_item->valueint;
    }

    bool previous = s_stream_enabled;
    s_stream_enabled = enable;

    if (previous != enable)
    {
        ESP_LOGI(TAG, "Stream %s (viewers=%d)", enable ? "enabled" : "paused", viewer_count);
    }
}

static void handle_control_message(const cJSON *root)
{
    if (!root)
    {
        return;
    }

    const cJSON *command_item = cJSON_GetObjectItem(root, "command");
    if (!command_item || !cJSON_IsString(command_item))
    {
        ESP_LOGW(TAG, "Control message sin comando válido");
        return;
    }

    const cJSON *vehicle_item = cJSON_GetObjectItem(root, "vehicle_id");
    const char *vehicle = (vehicle_item && cJSON_IsString(vehicle_item)) ? vehicle_item->valuestring : NULL;

    if (vehicle && vehicle[0] != '\0' && s_vehicle_id[0] != '\0' &&
        strncmp(vehicle, s_vehicle_id, sizeof(s_vehicle_id)) != 0)
    {
        ESP_LOGD(TAG, "Comando para otro vehículo (%s) - ignorado", vehicle);
        return;
    }

    control_message_t message = {
        .command = control_command_from_string(command_item->valuestring),
        .timestamp_ms = 0,
    };

    strncpy(message.raw_command, command_item->valuestring, sizeof(message.raw_command) - 1);
    message.raw_command[sizeof(message.raw_command) - 1] = '\0';

    const cJSON *timestamp_item = cJSON_GetObjectItem(root, "timestamp");
    if (timestamp_item && cJSON_IsNumber(timestamp_item))
    {
        message.timestamp_ms = (uint64_t)timestamp_item->valuedouble;
    }

    ESP_LOGD(TAG, "Control recibido: %s (%" PRIu64 " ms)",
             message.raw_command,
             message.timestamp_ms);

    if (s_control_callback)
    {
        s_control_callback(&message);
    }
}

static void handle_text_frame(const char *json_str)
{
    if (!json_str)
    {
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root)
    {
        ESP_LOGW(TAG, "JSON inválido: %s", json_str);
        return;
    }

    const cJSON *type_item = cJSON_GetObjectItem(root, "type");
    const char *type = (type_item && cJSON_IsString(type_item)) ? type_item->valuestring : NULL;

    if (!type)
    {
        ESP_LOGD(TAG, "Frame sin tipo - ignorado");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "stream_status") == 0)
    {
        handle_stream_status_message(root);
    }
    else if (strcmp(type, "control") == 0)
    {
        handle_control_message(root);
    }
    else
    {
        ESP_LOGD(TAG, "Mensaje %s sin handler", type);
    }

    cJSON_Delete(root);
}

static esp_err_t send_register_message(void)
{
    if (!s_client)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_vehicle_id[0] == '\0')
    {
        ESP_LOGE(TAG, "Vehicle ID no configurado, registro cancelado");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "register");
    cJSON_AddStringToObject(root, "role", "vehicle");
    cJSON_AddStringToObject(root, "vehicle_id", s_vehicle_id);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str)
    {
        return ESP_ERR_NO_MEM;
    }

    int sent = esp_websocket_client_send_text(s_client, json_str, strlen(json_str), portMAX_DELAY);
    free(json_str);

    if (sent < 0)
    {
        ESP_LOGE(TAG, "Error enviando registro de vehículo");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Registro de vehículo enviado (%s)", s_vehicle_id);
    return ESP_OK;
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
        s_stream_enabled = false;
        if (send_register_message() != ESP_OK)
        {
            ESP_LOGW(TAG, "No se pudo enviar registro del vehículo");
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected, will auto-reconnect...");
        s_is_connected = false;
        s_stream_enabled = false;
        break;

    case WEBSOCKET_EVENT_DATA:
        ESP_LOGD(TAG, "Received WebSocket data: opcode=%d, len=%d",
                 data->op_code, data->data_len);

        if (data->op_code == 0x01)
        { // Text frame (JSON control / status)
            char *json_str = malloc(data->data_len + 1);
            if (json_str)
            {
                memcpy(json_str, data->data_ptr, data->data_len);
                json_str[data->data_len] = '\0';

                handle_text_frame(json_str);

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

esp_err_t ws_client_init(const char *vehicle_id, control_callback_t callback)
{
    ESP_LOGI(TAG, "Initializing WebSocket client...");

    if (!vehicle_id || vehicle_id[0] == '\0')
    {
        ESP_LOGE(TAG, "Vehicle ID inválido");
        return ESP_ERR_INVALID_ARG;
    }

    s_control_callback = callback;
    strncpy(s_vehicle_id, vehicle_id, sizeof(s_vehicle_id) - 1);
    s_vehicle_id[sizeof(s_vehicle_id) - 1] = '\0';

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

bool ws_client_stream_enabled(void)
{
    return s_stream_enabled;
}

esp_err_t ws_client_send_frame(const uint8_t *frame, size_t length)
{
    if (!frame || length == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ws_client_is_connected())
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!ws_client_stream_enabled())
    {
        ESP_LOGV(TAG, "Streaming deshabilitado - descartando frame (%d bytes)", (int)length);
        return ESP_ERR_INVALID_STATE;
    }

    if (length > WS_MAX_PAYLOAD_SIZE)
    {
        ESP_LOGW(TAG, "JPEG demasiado grande (%d bytes > %d) - descartado",
                 (int)length, WS_MAX_PAYLOAD_SIZE);
        return ESP_ERR_INVALID_SIZE;
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
    s_stream_enabled = false;

    esp_err_t err = esp_websocket_client_stop(s_client);
    if (err == ESP_OK)
    {
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        ESP_LOGI(TAG, "WebSocket client disconnected");
    }

    return err;
}
