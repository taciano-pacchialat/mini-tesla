#include "ws_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "WebSocket";
static httpd_handle_t server = NULL;

// Lista de file descriptors de clientes WebSocket conectados
#define MAX_WS_CLIENTS 4
static int ws_client_fds[MAX_WS_CLIENTS] = {-1, -1, -1, -1};
static uint8_t ws_clients_count = 0;

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
    "body { font-family: Arial; margin: 20px; background: #1a1a1a; color: #fff; }"
    ".container { max-width: 1200px; margin: 0 auto; }"
    ".header { text-align: center; margin-bottom: 20px; }"
    ".video-container { position: relative; background: #000; margin: 20px 0; }"
    "#videoCanvas { width: 100%; max-width: 640px; height: auto; display: block; margin: 0 auto; }"
    ".telemetry { background: #2a2a2a; padding: 15px; border-radius: 5px; margin: 10px 0; }"
    ".telemetry-item { padding: 5px 0; border-bottom: 1px solid #444; }"
    ".telemetry-item:last-child { border-bottom: none; }"
    ".status { padding: 10px; border-radius: 5px; text-align: center; margin: 10px 0; }"
    ".status.connected { background: #0a4; color: #fff; }"
    ".status.disconnected { background: #a40; color: #fff; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<div class='header'><h1>🤖 ESP32-S3 Vision System</h1></div>"
    "<div id='status' class='status disconnected'>Desconectado</div>"
    "<div class='video-container'><canvas id='videoCanvas'></canvas></div>"
    "<div class='telemetry'>"
    "<h3>📊 Telemetría en Tiempo Real</h3>"
    "<div class='telemetry-item'>Objeto: <span id='object'>-</span></div>"
    "<div class='telemetry-item'>Distancia: <span id='distance'>-</span> cm</div>"
    "<div class='telemetry-item'>Píxel: (<span id='pixelX'>-</span>, <span id='pixelY'>-</span>)</div>"
    "<div class='telemetry-item'>Mundo: (<span id='worldX'>-</span>, <span id='worldY'>-</span>) cm</div>"
    "<div class='telemetry-item'>Píxeles: <span id='pixels'>-</span></div>"
    "<div class='telemetry-item'>FPS: <span id='fps'>-</span></div>"
    "</div>"
    "</div>"
    "<script>"
    "const canvas = document.getElementById('videoCanvas');"
    "const ctx = canvas.getContext('2d');"
    "const status = document.getElementById('status');"
    "let ws, frameCount = 0, lastTime = Date.now();"
    "function connect() {"
    "  ws = new WebSocket('ws://' + window.location.hostname + '/ws');"
    "  ws.binaryType = 'arraybuffer';"
    "  ws.onopen = () => { status.textContent = 'Conectado ✓'; status.className = 'status connected'; };"
    "  ws.onclose = () => { status.textContent = 'Desconectado ✗'; status.className = 'status disconnected'; setTimeout(connect, 3000); };"
    "  ws.onerror = (e) => { console.error('WebSocket error:', e); };"
    "  ws.onmessage = (e) => {"
    "    if (typeof e.data === 'string') {"
    "      const data = JSON.parse(e.data);"
    "      document.getElementById('object').textContent = data.detected ? data.object_type : 'No detectado';"
    "      document.getElementById('distance').textContent = data.detected ? data.distance_cm.toFixed(1) : '-';"
    "      document.getElementById('pixelX').textContent = data.detected ? data.pixel_x : '-';"
    "      document.getElementById('pixelY').textContent = data.detected ? data.pixel_y : '-';"
    "      document.getElementById('worldX').textContent = data.detected ? data.world_x.toFixed(1) : '-';"
    "      document.getElementById('worldY').textContent = data.detected ? data.world_y.toFixed(1) : '-';"
    "      document.getElementById('pixels').textContent = data.detected ? data.pixel_count : '-';"
    "    } else {"
    "      const img = new Image();"
    "      img.onload = () => {"
    "        canvas.width = img.width; canvas.height = img.height;"
    "        ctx.drawImage(img, 0, 0);"
    "        frameCount++;"
    "        const now = Date.now();"
    "        if (now - lastTime >= 1000) {"
    "          document.getElementById('fps').textContent = frameCount;"
    "          frameCount = 0; lastTime = now;"
    "        }"
    "      };"
    "      img.src = URL.createObjectURL(new Blob([e.data], {type: 'image/jpeg'}));"
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
        ESP_LOGI(TAG, "Handshake iniciado, fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Recibir frame para detectar nuevas conexiones o desconexiones
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
        return ret;
    }

    int fd = httpd_req_to_sockfd(req);

    // Si es un nuevo cliente, agregarlo a la lista
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT || ws_pkt.type == HTTPD_WS_TYPE_BINARY)
    {
        bool found = false;
        for (int i = 0; i < MAX_WS_CLIENTS; i++)
        {
            if (ws_client_fds[i] == fd)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            ws_add_client(fd);
        }
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

    // Enviar a todos los clientes conectados
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)json_string;
    ws_pkt.len = strlen(json_string);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_client_fds[i] != -1)
        {
            esp_err_t ret = httpd_ws_send_frame_async(server, ws_client_fds[i], &ws_pkt);
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "Error enviando a cliente fd=%d: %s", ws_client_fds[i], esp_err_to_name(ret));
                ws_remove_client(ws_client_fds[i]);
            }
        }
    }

    free(json_string);
    return ESP_OK;
}

esp_err_t ws_server_send_video_frame(const uint8_t *jpeg_data, size_t jpeg_len)
{
    if (!server || ws_clients_count == 0 || !jpeg_data || jpeg_len == 0)
    {
        return ESP_OK; // No hay clientes o datos inválidos
    }

    // Enviar frame binario (JPEG)
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)jpeg_data;
    ws_pkt.len = jpeg_len;
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;

    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_client_fds[i] != -1)
        {
            esp_err_t ret = httpd_ws_send_frame_async(server, ws_client_fds[i], &ws_pkt);
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "Error enviando frame a fd=%d: %s", ws_client_fds[i], esp_err_to_name(ret));
                ws_remove_client(ws_client_fds[i]);
            }
        }
    }

    return ESP_OK;
}

uint8_t ws_server_get_clients_count(void)
{
    return ws_clients_count;
}

bool ws_server_has_clients(void)
{
    return ws_clients_count > 0;
}
