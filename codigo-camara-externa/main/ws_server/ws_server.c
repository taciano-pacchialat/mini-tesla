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

// Lista de file descriptors de clientes WebSocket conectados
#define MAX_WS_CLIENTS 4
static int ws_client_fds[MAX_WS_CLIENTS] = {-1, -1, -1, -1};
static uint8_t ws_clients_count = 0;

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

static bool ws_client_exists(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_client_fds[i] == fd)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Agrega un cliente WebSocket a la lista
 */
static void ws_add_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_client_fds[i] == -1)
        {
            ws_client_fds[i] = fd;
            ws_clients_count++;
            ESP_LOGI(TAG, "Cliente WebSocket agregado, fd=%d, total=%d", fd, ws_clients_count);
            return;
        }
    }
    ESP_LOGW(TAG, "No hay espacio para más clientes WebSocket");
}

/**
 * @brief Remueve un cliente WebSocket de la lista
 */
static void ws_remove_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_client_fds[i] == fd)
        {
            ws_client_fds[i] = -1;
            ws_clients_count--;
            ESP_LOGI(TAG, "Cliente WebSocket removido, fd=%d, total=%d", fd, ws_clients_count);
            return;
        }
    }
}

/**
 * @brief Manejador de página web principal
 */
static const char *index_html =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>ESP32-S3 Vision System</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #101010; color: #fefefe; }"
    ".container { max-width: 1280px; margin: 0 auto; }"
    ".header { text-align: center; margin-bottom: 20px; }"
    ".status { padding: 10px; border-radius: 6px; text-align: center; margin-bottom: 20px; font-weight: bold; }"
    ".status.connected { background: #0a4; }"
    ".status.disconnected { background: #a40; }"
    ".video-grid { display: flex; flex-wrap: wrap; gap: 20px; }"
    ".card { background: #1f1f1f; border-radius: 10px; padding: 15px; flex: 1 1 360px; box-shadow: 0 8px 20px rgba(0,0,0,0.3); }"
    "canvas { width: 100%; height: auto; background: #000; border-radius: 6px; }"
    ".fps { margin-top: 8px; font-size: 0.9rem; color: #9fe070; }"
    ".telemetry { margin-top: 20px; background: #1b1b1b; border-radius: 10px; padding: 15px; }"
    ".telemetry h3 { margin-top: 0; }"
    ".telemetry-item { display: flex; justify-content: space-between; padding: 6px 0; border-bottom: 1px solid #2b2b2b; }"
    ".telemetry-item:last-child { border-bottom: none; }"
    "@media (max-width: 768px) { .video-grid { flex-direction: column; } }"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "  <div class='header'><h1>🤖 ESP32 Vision Dashboard</h1><p>SoftAP: ESP32-Vision-Bot (192.168.4.1)</p></div>"
    "  <div id='status' class='status disconnected'>WebSocket desconectado</div>"
    "  <div class='video-grid'>"
    "    <div class='card'>"
    "      <h3>ESP32-S3 (Nodo maestro)</h3>"
    "      <canvas id='canvasS3'></canvas>"
    "      <div class='fps'>FPS: <span id='fpsS3'>0</span></div>"
    "    </div>"
    "    <div class='card'>"
    "      <h3>ESP32-CAM (Vehículo)</h3>"
    "      <canvas id='canvasCar'></canvas>"
    "      <div class='fps'>FPS: <span id='fpsCar'>0</span></div>"
    "    </div>"
    "  </div>"
    "  <div class='telemetry'>"
    "    <h3>📊 Telemetría en Tiempo Real</h3>"
    "    <div class='telemetry-item'><span>Objeto</span><span id='object'>-</span></div>"
    "    <div class='telemetry-item'><span>Distancia</span><span><span id='distance'>-</span> cm</span></div>"
    "    <div class='telemetry-item'><span>Píxel</span><span>(<span id='pixelX'>-</span>, <span id='pixelY'>-</span>)</span></div>"
    "    <div class='telemetry-item'><span>Mundo</span><span>(<span id='worldX'>-</span>, <span id='worldY'>-</span>) cm</span></div>"
    "    <div class='telemetry-item'><span>Píxeles detectados</span><span id='pixels'>-</span></div>"
    "  </div>"
    "</div>"
    "<script>"
    "const canvases = { esp32s3: document.getElementById('canvasS3'), esp32cam: document.getElementById('canvasCar') };"
    "const contexts = { esp32s3: canvases.esp32s3.getContext('2d'), esp32cam: canvases.esp32cam.getContext('2d') };"
    "const statusEl = document.getElementById('status');"
    "const fpsLabels = { esp32s3: document.getElementById('fpsS3'), esp32cam: document.getElementById('fpsCar') };"
    "const fpsCounters = { esp32s3: {count: 0, last: Date.now()}, esp32cam: {count: 0, last: Date.now()} };"
    "let ws;"
    "let pendingFrameSource = 'esp32s3';"
    "function updateFps(source) {"
    "  const stats = fpsCounters[source];"
    "  stats.count++;"
    "  const now = Date.now();"
    "  if (now - stats.last >= 1000) {"
    "    fpsLabels[source].textContent = stats.count;"
    "    stats.count = 0;"
    "    stats.last = now;"
    "  }"
    "}"
    "function drawFrame(source, buffer) {"
    "  const blob = new Blob([buffer], {type: 'image/jpeg'});"
    "  const url = URL.createObjectURL(blob);"
    "  const img = new Image();"
    "  img.onload = () => {"
    "    const canvas = canvases[source];"
    "    const ctx = contexts[source];"
    "    canvas.width = img.width;"
    "    canvas.height = img.height;"
    "    ctx.drawImage(img, 0, 0);"
    "    URL.revokeObjectURL(url);"
    "    updateFps(source);"
    "  };"
    "  img.src = url;"
    "}"
    "function connect() {"
    "  ws = new WebSocket('ws://' + window.location.hostname + '/ws');"
    "  ws.binaryType = 'arraybuffer';"
    "  ws.onopen = () => { statusEl.textContent = 'WebSocket conectado'; statusEl.className = 'status connected'; };"
    "  ws.onclose = () => { statusEl.textContent = 'WebSocket desconectado'; statusEl.className = 'status disconnected'; setTimeout(connect, 3000); };"
    "  ws.onerror = (e) => console.error('WebSocket error', e);"
    "  ws.onmessage = (e) => {"
    "    if (typeof e.data === 'string') {"
    "      const data = JSON.parse(e.data);"
    "      if (data.type === 'frame') {"
    "        pendingFrameSource = data.source || 'esp32s3';"
    "        return;"
    "      }"
    "      document.getElementById('object').textContent = data.detected ? data.object_type : 'No detectado';"
    "      document.getElementById('distance').textContent = data.detected ? data.distance_cm.toFixed(1) : '-';"
    "      document.getElementById('pixelX').textContent = data.detected ? data.pixel_x : '-';"
    "      document.getElementById('pixelY').textContent = data.detected ? data.pixel_y : '-';"
    "      document.getElementById('worldX').textContent = data.detected ? data.world_x.toFixed(1) : '-';"
    "      document.getElementById('worldY').textContent = data.detected ? data.world_y.toFixed(1) : '-';"
    "      document.getElementById('pixels').textContent = data.detected ? data.pixel_count : '-';"
    "    } else {"
    "      drawFrame(pendingFrameSource, e.data);"
    "    }"
    "  };"
    "}"
    "connect();"
    "</script>"
    "</body>"
    "</html>";

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
        ESP_LOGD(TAG, "Frame de texto recibido de fd=%d: %s", fd, (char *)ws_pkt.payload);
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
        for (int i = 0; i < MAX_WS_CLIENTS; i++)
        {
            ws_client_fds[i] = -1;
        }
        ESP_LOGI(TAG, "Servidor WebSocket detenido");
    }
    return ESP_OK;
}

esp_err_t ws_server_send_telemetry(const telemetry_data_t *telemetry)
{
    if (!server || ws_clients_count == 0)
    {
        return ESP_OK; // No hay clientes, no hacer nada
    }

    // Crear JSON con los datos de telemetría
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "object_type", telemetry->object_type);
    cJSON_AddNumberToObject(root, "distance_cm", telemetry->distance_cm);
    cJSON_AddNumberToObject(root, "angle_deg", telemetry->angle_deg);
    cJSON_AddNumberToObject(root, "pixel_x", telemetry->pixel_x);
    cJSON_AddNumberToObject(root, "pixel_y", telemetry->pixel_y);
    cJSON_AddNumberToObject(root, "world_x", telemetry->world_x);
    cJSON_AddNumberToObject(root, "world_y", telemetry->world_y);
    cJSON_AddNumberToObject(root, "pixel_count", telemetry->pixel_count);
    cJSON_AddBoolToObject(root, "detected", telemetry->detected);
    cJSON_AddNumberToObject(root, "timestamp_ms", telemetry->timestamp_ms);

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_string)
    {
        ESP_LOGE(TAG, "Error creando JSON");
        return ESP_FAIL;
    }

    size_t json_len = strlen(json_string);
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_client_fds[i] == -1)
        {
            continue;
        }

        esp_err_t ret = ws_queue_frame(ws_client_fds[i],
                                       HTTPD_WS_TYPE_TEXT,
                                       (const uint8_t *)json_string,
                                       json_len);
        if (ret != ESP_OK)
        {
            ws_remove_client(ws_client_fds[i]);
        }
    }

    free(json_string);
    return ESP_OK;
}

static esp_err_t broadcast_video_frame(frame_source_t source,
                                       const uint8_t *jpeg_data,
                                       size_t jpeg_len,
                                       int exclude_fd)
{
    if (!server || ws_clients_count == 0 || !jpeg_data || jpeg_len == 0)
    {
        return ESP_OK; // No hay clientes o datos inválidos
    }

    const char *source_str = frame_source_to_string(source);
    char meta[64];
    int meta_len = snprintf(meta, sizeof(meta),
                            "{\"type\":\"frame\",\"source\":\"%s\"}",
                            source_str);

    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_client_fds[i] == -1)
        {
            continue;
        }

        if (exclude_fd >= 0 && ws_client_fds[i] == exclude_fd)
        {
            continue;
        }

        esp_err_t ret = ws_queue_frame(ws_client_fds[i],
                                       HTTPD_WS_TYPE_TEXT,
                                       (const uint8_t *)meta,
                                       meta_len);
        if (ret != ESP_OK)
        {
            ws_remove_client(ws_client_fds[i]);
            continue;
        }

        ret = ws_queue_frame(ws_client_fds[i],
                             HTTPD_WS_TYPE_BINARY,
                             jpeg_data,
                             jpeg_len);
        if (ret != ESP_OK)
        {
            ws_remove_client(ws_client_fds[i]);
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
    return ws_clients_count > 0;
}
