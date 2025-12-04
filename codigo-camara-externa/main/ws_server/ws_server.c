#include "ws_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "WebSocket";
static httpd_handle_t server = NULL;

// Gestión de clientes WebSocket (dashboard y vehículos)
#define MAX_WS_CLIENTS 4

typedef enum
{
    WS_ROLE_UNKNOWN = 0,
    WS_ROLE_DASHBOARD,
    WS_ROLE_VEHICLE,
} ws_role_t;

typedef struct
{
    int fd;
    ws_role_t role;
    char vehicle_id[32];
} ws_client_t;

static ws_client_t ws_clients[MAX_WS_CLIENTS] = {
    {.fd = -1, .role = WS_ROLE_UNKNOWN, .vehicle_id = ""},
    {.fd = -1, .role = WS_ROLE_UNKNOWN, .vehicle_id = ""},
    {.fd = -1, .role = WS_ROLE_UNKNOWN, .vehicle_id = ""},
    {.fd = -1, .role = WS_ROLE_UNKNOWN, .vehicle_id = ""},
};
static uint8_t ws_clients_count = 0;
static uint8_t ws_dashboard_count = 0;

static void ws_remove_client(int fd);
static void ws_update_stream_status_for_vehicles(void);
static esp_err_t ws_queue_frame(int fd,
                                httpd_ws_type_t type,
                                const uint8_t *data,
                                size_t len);

static ws_client_t *ws_find_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].fd == fd)
        {
            return &ws_clients[i];
        }
    }
    return NULL;
}

static ws_client_t *ws_find_vehicle_by_id(const char *vehicle_id)
{
    if (!vehicle_id || vehicle_id[0] == '\0')
    {
        return NULL;
    }

    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].fd == -1 || ws_clients[i].role != WS_ROLE_VEHICLE)
        {
            continue;
        }

        if (strncmp(ws_clients[i].vehicle_id, vehicle_id, sizeof(ws_clients[i].vehicle_id)) == 0)
        {
            return &ws_clients[i];
        }
    }
    return NULL;
}

static ws_client_t *ws_first_vehicle(void)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].fd != -1 && ws_clients[i].role == WS_ROLE_VEHICLE)
        {
            return &ws_clients[i];
        }
    }
    return NULL;
}

static void ws_set_client_role(ws_client_t *client,
                               ws_role_t role,
                               const char *vehicle_id)
{
    if (!client)
    {
        return;
    }

    ws_role_t previous_role = client->role;

    if (client->role == WS_ROLE_DASHBOARD && role != WS_ROLE_DASHBOARD && ws_dashboard_count > 0)
    {
        ws_dashboard_count--;
    }

    client->role = role;

    if (role == WS_ROLE_DASHBOARD)
    {
        ws_dashboard_count++;
    }

    if (role == WS_ROLE_VEHICLE && vehicle_id)
    {
        strncpy(client->vehicle_id, vehicle_id, sizeof(client->vehicle_id) - 1);
        client->vehicle_id[sizeof(client->vehicle_id) - 1] = '\0';
    }
    else if (role != WS_ROLE_VEHICLE)
    {
        client->vehicle_id[0] = '\0';
    }

    if ((previous_role != role) &&
        (previous_role == WS_ROLE_DASHBOARD || role == WS_ROLE_DASHBOARD))
    {
        ws_update_stream_status_for_vehicles();
    }
}

static esp_err_t ws_send_vehicle_list_to_client(const ws_client_t *dashboard)
{
    if (!dashboard || dashboard->fd < 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "vehicle_list");
    cJSON *list = cJSON_AddArrayToObject(root, "vehicles");
    if (!list)
    {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].fd == -1 || ws_clients[i].role != WS_ROLE_VEHICLE)
        {
            continue;
        }

        if (ws_clients[i].vehicle_id[0] != '\0')
        {
            cJSON_AddItemToArray(list, cJSON_CreateString(ws_clients[i].vehicle_id));
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json)
    {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ws_queue_frame(dashboard->fd,
                                   HTTPD_WS_TYPE_TEXT,
                                   (const uint8_t *)json,
                                   strlen(json));
    free(json);
    return ret;
}

static void ws_broadcast_vehicle_list(void)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].fd == -1 || ws_clients[i].role != WS_ROLE_DASHBOARD)
        {
            continue;
        }

        if (ws_send_vehicle_list_to_client(&ws_clients[i]) != ESP_OK)
        {
            ws_remove_client(ws_clients[i].fd);
        }
    }
}

static esp_err_t ws_send_stream_status_to_vehicle(const ws_client_t *vehicle)
{
    if (!vehicle || vehicle->fd < 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "stream_status");
    cJSON_AddBoolToObject(root, "enable", ws_dashboard_count > 0);
    cJSON_AddNumberToObject(root, "viewer_count", ws_dashboard_count);

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_string)
    {
        ESP_LOGE(TAG, "Error creando JSON para estado de stream");
        return ESP_FAIL;
    }

    esp_err_t ret = ws_queue_frame(vehicle->fd,
                                   HTTPD_WS_TYPE_TEXT,
                                   (const uint8_t *)json_string,
                                   strlen(json_string));
    free(json_string);

    return ret;
}

static void ws_update_stream_status_for_vehicles(void)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].fd == -1 || ws_clients[i].role != WS_ROLE_VEHICLE)
        {
            continue;
        }

        if (ws_send_stream_status_to_vehicle(&ws_clients[i]) != ESP_OK)
        {
            ws_remove_client(ws_clients[i].fd);
        }
    }
}

static bool ws_client_exists(int fd)
{
    return ws_find_client(fd) != NULL;
}

static void ws_add_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].fd == -1)
        {
            ws_clients[i].fd = fd;
            ws_clients[i].role = WS_ROLE_UNKNOWN;
            ws_clients[i].vehicle_id[0] = '\0';
            ws_clients_count++;
            ESP_LOGI(TAG, "Cliente WebSocket agregado, fd=%d, total=%d", fd, ws_clients_count);
            return;
        }
    }
    ESP_LOGW(TAG, "No hay espacio para más clientes WebSocket");
}

static esp_err_t ws_forward_control_message(const cJSON *root,
                                            ws_client_t *source_client)
{
    if (!root)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *vehicle_id_item = cJSON_GetObjectItem(root, "vehicle_id");
    const char *vehicle_id = (vehicle_id_item && cJSON_IsString(vehicle_id_item)) ? vehicle_id_item->valuestring : NULL;

    ws_client_t *target = ws_find_vehicle_by_id(vehicle_id);
    if (!target)
    {
        target = ws_first_vehicle();
    }

    if (!target)
    {
        ESP_LOGW(TAG, "No hay vehículos conectados para reenviar comando");
        return ESP_FAIL;
    }

    if (source_client && target->fd == source_client->fd)
    {
        ESP_LOGW(TAG, "Ignorando comando porque el origen es el mismo vehículo");
        return ESP_FAIL;
    }

    char *json_string = cJSON_PrintUnformatted(root);
    if (!json_string)
    {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ws_queue_frame(target->fd,
                                   HTTPD_WS_TYPE_TEXT,
                                   (const uint8_t *)json_string,
                                   strlen(json_string));

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reenviando comando a fd=%d: %s",
                 target->fd,
                 esp_err_to_name(ret));
        ws_remove_client(target->fd);
    }

    free(json_string);
    return ret;
}

static void ws_handle_text_message(int fd, const char *payload)
{
    if (!payload)
    {
        return;
    }

    ws_client_t *client = ws_find_client(fd);
    if (!client)
    {
        ws_add_client(fd);
        client = ws_find_client(fd);
    }

    cJSON *root = cJSON_Parse(payload);
    if (!root)
    {
        ESP_LOGW(TAG, "JSON inválido recibido de fd=%d", fd);
        return;
    }

    const cJSON *type_item = cJSON_GetObjectItem(root, "type");
    const char *type = (type_item && cJSON_IsString(type_item)) ? type_item->valuestring : NULL;

    if (!type)
    {
        ESP_LOGD(TAG, "Mensaje sin tipo desde fd=%d", fd);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "register") == 0)
    {
        const cJSON *role_item = cJSON_GetObjectItem(root, "role");
        const char *role = (role_item && cJSON_IsString(role_item)) ? role_item->valuestring : NULL;
        const cJSON *vehicle_item = cJSON_GetObjectItem(root, "vehicle_id");
        const char *vehicle_id = (vehicle_item && cJSON_IsString(vehicle_item)) ? vehicle_item->valuestring : NULL;

        if (role && strcmp(role, "vehicle") == 0)
        {
            ws_set_client_role(client, WS_ROLE_VEHICLE, vehicle_id ? vehicle_id : "");
            ESP_LOGI(TAG, "Vehículo registrado: fd=%d, id=%s",
                     fd,
                     vehicle_id ? vehicle_id : "(sin id)");
            ws_broadcast_vehicle_list();
            ws_send_stream_status_to_vehicle(client);
        }
        else
        {
            ws_set_client_role(client, WS_ROLE_DASHBOARD, NULL);
            ESP_LOGI(TAG, "Dashboard registrado: fd=%d", fd);
            ws_send_vehicle_list_to_client(client);
        }
    }
    else if (strcmp(type, "control") == 0)
    {
        if (client && client->role == WS_ROLE_VEHICLE)
        {
            ESP_LOGW(TAG, "Vehículo envió comando de control - ignorado");
        }
        else
        {
            ws_set_client_role(client, WS_ROLE_DASHBOARD, NULL);
            ws_forward_control_message(root, client);
        }
    }
    else
    {
        ESP_LOGD(TAG, "Mensaje ignorado (%s) desde fd=%d", type, fd);
    }

    cJSON_Delete(root);
}

typedef struct
{
    httpd_handle_t server;
    int fd;
    httpd_ws_frame_t frame;
    uint8_t payload[];
} ws_async_packet_t;

static void ws_async_send_handler(void *arg)
{
    ws_async_packet_t *packet = (ws_async_packet_t *)arg;

    if (packet->frame.len > 0)
    {
        packet->frame.payload = packet->payload;
    }
    else
    {
        packet->frame.payload = NULL;
    }

    esp_err_t err = httpd_ws_send_frame_async(packet->server, packet->fd, &packet->frame);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Error enviando frame a fd=%d: %s", packet->fd, esp_err_to_name(err));
    }

    free(packet);
}

static esp_err_t ws_queue_frame(int fd,
                                httpd_ws_type_t type,
                                const uint8_t *data,
                                size_t len)
{
    if (!server)
    {
        return ESP_ERR_INVALID_STATE;
    }

    size_t alloc_size = sizeof(ws_async_packet_t) + len;
    ws_async_packet_t *packet = (ws_async_packet_t *)malloc(alloc_size);
    if (!packet)
    {
        ESP_LOGE(TAG, "Sin memoria para enviar frame (%zu bytes)", len);
        return ESP_ERR_NO_MEM;
    }

    packet->server = server;
    packet->fd = fd;
    memset(&packet->frame, 0, sizeof(packet->frame));
    packet->frame.type = type;
    packet->frame.len = len;

    if (len > 0 && data)
    {
        memcpy(packet->payload, data, len);
    }

    esp_err_t err = httpd_queue_work(server, ws_async_send_handler, packet);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "No se pudo encolar frame para fd=%d: %s", fd, esp_err_to_name(err));
        free(packet);
    }
    return err;
}

static const char *frame_source_to_string(frame_source_t source)
{
    switch (source)
    {
    case FRAME_SOURCE_ESP32CAM:
        return "esp32cam";
    case FRAME_SOURCE_ESP32S3:
    default:
        return "esp32s3";
    }
}

static esp_err_t broadcast_video_frame(frame_source_t source,
                                       const uint8_t *jpeg_data,
                                       size_t jpeg_len,
                                       int exclude_fd);

static void ws_remove_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].fd != fd)
        {
            continue;
        }

        bool was_vehicle = (ws_clients[i].role == WS_ROLE_VEHICLE && ws_clients[i].vehicle_id[0] != '\0');
        bool was_dashboard = (ws_clients[i].role == WS_ROLE_DASHBOARD);

        if (was_dashboard && ws_dashboard_count > 0)
        {
            ws_dashboard_count--;
            ws_update_stream_status_for_vehicles();
        }

        ws_clients[i].fd = -1;
        ws_clients[i].role = WS_ROLE_UNKNOWN;
        ws_clients[i].vehicle_id[0] = '\0';
        if (ws_clients_count > 0)
        {
            ws_clients_count--;
        }

        ESP_LOGI(TAG, "Cliente WebSocket removido, fd=%d, total=%d", fd, ws_clients_count);

        if (was_vehicle)
        {
            ws_broadcast_vehicle_list();
        }
        return;
    }
}

/**
 * @brief Manejador de página web principal
 */
static const char *index_html =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<title>ESP32 Vision Control</title>\n"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>\n"
    "<style>\n"
    "body { font-family: 'Space Grotesk', 'Segoe UI', sans-serif; margin: 0; padding: 24px; background: radial-gradient(circle at top,#0f1f3d,#050910 70%); color: #f7fafc; }\n"
    ".container { max-width: 1280px; margin: 0 auto; }\n"
    ".header { text-align: center; margin-bottom: 24px; }\n"
    ".status { padding: 12px; border-radius: 999px; text-align: center; margin-bottom: 24px; font-weight: 600; letter-spacing: 0.04em; text-transform: uppercase; }\n"
    ".status.connected { background: rgba(88,225,193,0.12); color: #58e1c1; border: 1px solid rgba(88,225,193,0.4); }\n"
    ".status.disconnected { background: rgba(242,95,92,0.12); color: #f25f5c; border: 1px solid rgba(242,95,92,0.4); }\n"
    ".video-grid { display: flex; flex-wrap: wrap; gap: 20px; }\n"
    ".card { background: rgba(16,25,45,0.92); border-radius: 16px; padding: 18px; flex: 1 1 360px; box-shadow: 0 25px 60px rgba(2,6,23,0.6); border: 1px solid rgba(255,255,255,0.04); backdrop-filter: blur(6px); }\n"
    ".card h3 { margin: 0 0 10px; letter-spacing: 0.05em; }\n"
    "canvas { width: 100%; height: auto; background: #000; border-radius: 10px; border: 1px solid rgba(255,255,255,0.05); }\n"
    ".fps { margin-top: 8px; font-size: 0.85rem; color: #58e1c1; letter-spacing: 0.05em; }\n"
    ".control-panel { margin-top: 24px; background: rgba(16,25,45,0.92); border-radius: 16px; padding: 18px; box-shadow: 0 25px 60px rgba(2,6,23,0.5); border: 1px solid rgba(255,255,255,0.04); }\n"
    ".control-panel h3 { margin-top: 0; letter-spacing: 0.08em; text-transform: uppercase; font-size: 0.95rem; color: #9fabc7; }\n"
    ".control-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 120px)); gap: 12px; justify-content: center; margin-top: 10px; }\n"
    ".control-btn { background: #0c1426; border: 1px solid rgba(88,225,193,0.3); color: #f7fafc; font-size: 1rem; font-weight: 600; padding: 14px 10px; border-radius: 12px; text-transform: uppercase; letter-spacing: 0.08em; cursor: pointer; transition: transform 0.15s ease, border-color 0.15s ease, background 0.15s ease; }\n"
    ".control-btn:disabled { opacity: 0.3; cursor: not-allowed; }\n"
    ".control-btn.active, .control-btn:focus-visible { border-color: #58e1c1; background: rgba(88,225,193,0.18); outline: none; transform: translateY(-2px); }\n"
    ".control-btn.secondary { border-color: rgba(247,250,252,0.2); color: #9fabc7; }\n"
    ".control-helper { margin-top: 12px; font-size: 0.85rem; color: #9fabc7; text-align: center; letter-spacing: 0.05em; }\n"
    "label { display: block; font-size: 0.85rem; color: #9fabc7; letter-spacing: 0.05em; margin-bottom: 6px; }\n"
    "select { width: 100%; padding: 10px 12px; border-radius: 10px; border: 1px solid rgba(255,255,255,0.08); background: rgba(8,14,26,0.8); color: #f7fafc; font-size: 0.95rem; }\n"
    "@media (max-width: 768px) { .video-grid { flex-direction: column; } .control-grid { grid-template-columns: repeat(3, minmax(0, 1fr)); } }\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<div class='container'>\n"
    "  <div class='header'><h1>ESP32 Vision Dashboard</h1><p>SoftAP: ESP32-Vision-Bot (192.168.4.1)</p></div>\n"
    "  <div id='status' class='status disconnected'>WebSocket desconectado</div>\n"
    "  <div class='video-grid'>\n"
    "    <div class='card'>\n"
    "      <h3>ESP32-S3 (Nodo maestro)</h3>\n"
    "      <canvas id='canvasS3'></canvas>\n"
    "      <div class='fps'>FPS: <span id='fpsS3'>0</span></div>\n"
    "    </div>\n"
    "    <div class='card'>\n"
    "      <h3>ESP32-CAM (Vehiculo)</h3>\n"
    "      <canvas id='canvasCar'></canvas>\n"
    "      <div class='fps'>FPS: <span id='fpsCar'>0</span></div>\n"
    "    </div>\n"
    "  </div>\n"
    "  <div class='control-panel'>\n"
    "    <h3>Control Manual del Vehículo</h3>\n"
    "    <label for='vehicleSelect'>Vehiculo conectado</label>\n"
    "    <select id='vehicleSelect'></select>\n"
    "    <div class='control-grid'>\n"
    "      <div></div>\n"
    "      <button class='control-btn' data-control='forward'>Adelante</button>\n"
    "      <div></div>\n"
    "      <button class='control-btn' data-control='left'>Izquierda</button>\n"
    "      <button class='control-btn secondary' data-control='stop'>Stop</button>\n"
    "      <button class='control-btn' data-control='right'>Derecha</button>\n"
    "      <div></div>\n"
    "      <button class='control-btn' data-control='backward'>Atras</button>\n"
    "      <div></div>\n"
    "    </div>\n"
    "    <div class='control-helper'>Manten presionado para avanzar; suelta para frenar. Tambien puedes usar WASD o las flechas.</div>\n"
    "  </div>\n"
    "</div>\n"
    "<script>\n"
    "const canvases = { esp32s3: document.getElementById('canvasS3'), esp32cam: document.getElementById('canvasCar') };\n"
    "const contexts = { esp32s3: canvases.esp32s3.getContext('2d'), esp32cam: canvases.esp32cam.getContext('2d') };\n"
    "const statusEl = document.getElementById('status');\n"
    "const fpsLabels = { esp32s3: document.getElementById('fpsS3'), esp32cam: document.getElementById('fpsCar') };\n"
    "const fpsCounters = { esp32s3: {count: 0, last: Date.now()}, esp32cam: {count: 0, last: Date.now()} };\n"
    "const controlButtons = document.querySelectorAll('.control-btn');\n"
    "const vehicleSelect = document.getElementById('vehicleSelect');\n"
    "const commandIntervals = new Map();\n"
    "const keyboardMap = { ArrowUp: 'forward', KeyW: 'forward', ArrowDown: 'backward', KeyS: 'backward', ArrowLeft: 'left', KeyA: 'left', ArrowRight: 'right', KeyD: 'right', Space: 'stop' };\n"
    "const pressedKeys = new Set();\n"
    "let ws;\n"
    "let pendingFrameSource = 'esp32s3';\n"
    "let selectedVehicleId = null;\n"
    "\n"
    "function setControlsEnabled(enabled) {\n"
    "  controlButtons.forEach(btn => {\n"
    "    btn.disabled = !enabled;\n"
    "    if (!enabled) { btn.classList.remove('active'); }\n"
    "  });\n"
    "  vehicleSelect.disabled = !enabled;\n"
    "  if (!enabled) {\n"
    "    commandIntervals.forEach(interval => clearInterval(interval));\n"
    "    commandIntervals.clear();\n"
    "  }\n"
    "}\n"
    "\n"
    "function updateVehicleOptions(list = []) {\n"
    "  vehicleSelect.innerHTML = '';\n"
    "  list.forEach(id => {\n"
    "    const option = document.createElement('option');\n"
    "    option.value = id;\n"
    "    option.textContent = id;\n"
    "    vehicleSelect.appendChild(option);\n"
    "  });\n"
    "  selectedVehicleId = list.length ? list[0] : null;\n"
    "  setControlsEnabled(!!selectedVehicleId && ws && ws.readyState === WebSocket.OPEN);\n"
    "}\n"
    "\n"
    "vehicleSelect.addEventListener('change', () => {\n"
    "  selectedVehicleId = vehicleSelect.value || null;\n"
    "});\n"
    "\n"
    "function sendControl(command) {\n"
    "  if (!ws || ws.readyState !== WebSocket.OPEN || !selectedVehicleId) { return; }\n"
    "  const payload = { type: 'control', command, vehicle_id: selectedVehicleId, timestamp: Date.now() };\n"
    "  ws.send(JSON.stringify(payload));\n"
    "}\n"
    "\n"
    "function attachControlHandlers() {\n"
    "  controlButtons.forEach(btn => {\n"
    "    const command = btn.dataset.control;\n"
    "    const start = (event) => {\n"
    "      event.preventDefault();\n"
    "      if (btn.disabled) { return; }\n"
    "      btn.classList.add('active');\n"
    "      sendControl(command);\n"
    "      const interval = setInterval(() => sendControl(command), 350);\n"
    "      commandIntervals.set(btn, interval);\n"
    "    };\n"
    "    const stop = () => {\n"
    "      btn.classList.remove('active');\n"
    "      const interval = commandIntervals.get(btn);\n"
    "      if (interval) { clearInterval(interval); commandIntervals.delete(btn); }\n"
    "      if (command !== 'stop') { sendControl('stop'); }\n"
    "    };\n"
    "    btn.addEventListener('pointerdown', start);\n"
    "    btn.addEventListener('pointerup', stop);\n"
    "    btn.addEventListener('pointerleave', stop);\n"
    "    btn.addEventListener('pointercancel', stop);\n"
    "  });\n"
    "}\n"
    "\n"
    "attachControlHandlers();\n"
    "setControlsEnabled(false);\n"
    "\n"
    "function updateFps(source) {\n"
    "  const stats = fpsCounters[source];\n"
    "  stats.count++;\n"
    "  const now = Date.now();\n"
    "  if (now - stats.last >= 1000) {\n"
    "    fpsLabels[source].textContent = stats.count;\n"
    "    stats.count = 0;\n"
    "    stats.last = now;\n"
    "  }\n"
    "}\n"
    "\n"
    "function drawFrame(source, buffer) {\n"
    "  const blob = new Blob([buffer], {type: 'image/jpeg'});\n"
    "  const url = URL.createObjectURL(blob);\n"
    "  const img = new Image();\n"
    "  img.onload = () => {\n"
    "    const canvas = canvases[source];\n"
    "    const ctx = contexts[source];\n"
    "    canvas.width = img.width;\n"
    "    canvas.height = img.height;\n"
    "    ctx.drawImage(img, 0, 0);\n"
    "    URL.revokeObjectURL(url);\n"
    "    updateFps(source);\n"
    "  };\n"
    "  img.src = url;\n"
    "}\n"
    "\n"
    "function connect() {\n"
    "  ws = new WebSocket('ws://' + window.location.hostname + '/ws');\n"
    "  ws.binaryType = 'arraybuffer';\n"
    "  ws.onopen = () => {\n"
    "    statusEl.textContent = 'WebSocket conectado';\n"
    "    statusEl.className = 'status connected';\n"
    "    ws.send(JSON.stringify({ type: 'register', role: 'dashboard' }));\n"
    "  };\n"
    "  ws.onclose = () => {\n"
    "    statusEl.textContent = 'WebSocket desconectado';\n"
    "    statusEl.className = 'status disconnected';\n"
    "    updateVehicleOptions([]);\n"
    "    setTimeout(connect, 2000);\n"
    "  };\n"
    "  ws.onerror = (e) => console.error('WebSocket error', e);\n"
    "  ws.onmessage = (e) => {\n"
    "    if (typeof e.data === 'string') {\n"
    "      const data = JSON.parse(e.data);\n"
    "      if (data.type === 'frame') {\n"
    "        pendingFrameSource = data.source || 'esp32s3';\n"
    "        return;\n"
    "      }\n"
    "      if (data.type === 'vehicle_list') {\n"
    "        updateVehicleOptions(data.vehicles || []);\n"
    "        return;\n"
    "      }\n"
    "      return;\n"
    "    }\n"
    "    drawFrame(pendingFrameSource, e.data);\n"
    "  };\n"
    "}\n"
    "\n"
    "connect();\n"
    "\n"
    "document.addEventListener('keydown', (event) => {\n"
    "  const command = keyboardMap[event.code];\n"
    "  if (!command || pressedKeys.has(event.code)) { return; }\n"
    "  pressedKeys.add(event.code);\n"
    "  const button = [...controlButtons].find(btn => btn.dataset.control === command);\n"
    "  if (button && !button.disabled) { button.classList.add('active'); }\n"
    "  sendControl(command);\n"
    "});\n"
    "\n"
    "document.addEventListener('keyup', (event) => {\n"
    "  if (!pressedKeys.has(event.code)) { return; }\n"
    "  pressedKeys.delete(event.code);\n"
    "  const active = [...controlButtons].filter(btn => btn.classList.contains('active') && btn.dataset.control !== 'stop');\n"
    "  active.forEach(btn => btn.classList.remove('active'));\n"
    "  sendControl('stop');\n"
    "});\n"
    "\n"
    "window.addEventListener('blur', () => {\n"
    "  pressedKeys.clear();\n"
    "  sendControl('stop');\n"
    "  controlButtons.forEach(btn => btn.classList.remove('active'));\n"
    "});\n"
    "</script>\n"
    "</body>\n"
    "</html>\n";

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief Manejador de WebSocket
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "Handshake iniciado, fd=%d", fd);

        if (!ws_client_exists(fd))
        {
            ws_add_client(fd);
        }
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
        return ret;
    }

    int fd = httpd_req_to_sockfd(req);

    if (ws_pkt.len > 0)
    {
        ws_pkt.payload = malloc(ws_pkt.len + 1);
        if (ws_pkt.payload == NULL)
        {
            ESP_LOGE(TAG, "No hay memoria para payload WebSocket");
            return ESP_ERR_NO_MEM;
        }

        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame payload failed: %s", esp_err_to_name(ret));
            free(ws_pkt.payload);
            return ret;
        }

        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
        {
            ((uint8_t *)ws_pkt.payload)[ws_pkt.len] = '\0';
        }
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE)
    {
        ws_remove_client(fd);
        if (ws_pkt.payload)
        {
            free(ws_pkt.payload);
        }
        return ESP_OK;
    }

    // Si llegó cualquier frame válido y no estaba registrado, agregarlo
    if (!ws_client_exists(fd))
    {
        ws_add_client(fd);
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_BINARY && ws_pkt.len > 0 && ws_pkt.payload)
    {
        ESP_LOGD(TAG, "Frame binario recibido de fd=%d (%d bytes)", fd, ws_pkt.len);
        broadcast_video_frame(FRAME_SOURCE_ESP32CAM, ws_pkt.payload, ws_pkt.len, fd);
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.payload)
    {
        ws_handle_text_message(fd, (char *)ws_pkt.payload);
    }

    if (ws_pkt.payload)
    {
        free(ws_pkt.payload);
    }

    return ESP_OK;
}

esp_err_t ws_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7; // Permitir múltiples conexiones
    config.lru_purge_enable = true;
    config.core_id = 0; // Ejecutar en Core 0 (Protocol CPU)

    ESP_LOGI(TAG, "Iniciando servidor HTTP en puerto %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Error iniciando servidor HTTP");
        return ESP_FAIL;
    }

    // Registrar manejador de página principal
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &index_uri);

    // Registrar manejador de WebSocket
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true};
    httpd_register_uri_handler(server, &ws_uri);

    ESP_LOGI(TAG, "╔════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║      Servidor WebSocket Iniciado               ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ URL:           http://192.168.4.1              ║");
    ESP_LOGI(TAG, "║ WebSocket:     ws://192.168.4.1/ws             ║");
    ESP_LOGI(TAG, "║ Core Affinity: Core 0 (Protocol CPU)           ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════╝");

    return ESP_OK;
}

esp_err_t ws_server_stop(void)
{
    if (server)
    {
        httpd_stop(server);
        server = NULL;
        ws_clients_count = 0;
        ws_dashboard_count = 0;
        for (int i = 0; i < MAX_WS_CLIENTS; i++)
        {
            ws_clients[i].fd = -1;
            ws_clients[i].role = WS_ROLE_UNKNOWN;
            ws_clients[i].vehicle_id[0] = '\0';
        }
        ESP_LOGI(TAG, "Servidor WebSocket detenido");
    }
    return ESP_OK;
}

static esp_err_t broadcast_video_frame(frame_source_t source,
                                       const uint8_t *jpeg_data,
                                       size_t jpeg_len,
                                       int exclude_fd)
{
    if (!server || !jpeg_data || jpeg_len == 0 || ws_dashboard_count == 0)
    {
        return ESP_OK;
    }

    const char *source_str = frame_source_to_string(source);
    char meta[64];
    int meta_len = snprintf(meta, sizeof(meta),
                            "{\"type\":\"frame\",\"source\":\"%s\"}",
                            source_str);

    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].fd == -1 || ws_clients[i].role != WS_ROLE_DASHBOARD)
        {
            continue;
        }

        if (exclude_fd >= 0 && ws_clients[i].fd == exclude_fd)
        {
            continue;
        }

        esp_err_t ret = ws_queue_frame(ws_clients[i].fd,
                                       HTTPD_WS_TYPE_TEXT,
                                       (const uint8_t *)meta,
                                       meta_len);
        if (ret != ESP_OK)
        {
            ws_remove_client(ws_clients[i].fd);
            continue;
        }

        ret = ws_queue_frame(ws_clients[i].fd,
                             HTTPD_WS_TYPE_BINARY,
                             jpeg_data,
                             jpeg_len);
        if (ret != ESP_OK)
        {
            ws_remove_client(ws_clients[i].fd);
        }
    }

    return ESP_OK;
}

esp_err_t ws_server_send_video_frame(frame_source_t source,
                                     const uint8_t *jpeg_data,
                                     size_t jpeg_len)
{
    return broadcast_video_frame(source, jpeg_data, jpeg_len, -1);
}

uint8_t ws_server_get_clients_count(void)
{
    return ws_clients_count;
}

bool ws_server_has_clients(void)
{
    return ws_dashboard_count > 0;
}
