#include "webserver.h"
#include "../camera_driver/camera_driver.h"
#include "../vision/vision.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_camera.h"
#include <string.h>

static const char *TAG = "Webserver";
static httpd_handle_t server = NULL;

// Variable global para configurar qué color detectar
static const color_range_t *current_color_range = &COLOR_GREEN; // Por defecto verde

// Función auxiliar para convertir RGB565 a JPEG
static bool rgb565_to_jpeg(camera_fb_t *fb, uint8_t **jpg_buf, size_t *jpg_len, int quality)
{
    if (!fb || fb->format != PIXFORMAT_RGB565)
    {
        return false;
    }

    // Allocate temporary buffer for JPEG
    size_t jpg_buf_len = fb->width * fb->height / 5; // Estimación
    uint8_t *jpg = (uint8_t *)malloc(jpg_buf_len);
    if (!jpg)
    {
        ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
        return false;
    }

    // Convert RGB565 to JPEG using frame2jpg (función del componente esp32-camera)
    bool converted = frame2jpg(fb, quality, &jpg, jpg_len);

    if (!converted)
    {
        free(jpg);
        ESP_LOGE(TAG, "RGB565 to JPEG conversion failed");
        return false;
    }

    *jpg_buf = jpg;
    return true;
}

// HTML page with embedded JavaScript for video streaming
static const char STREAM_HTML[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <title>ESP32 Camera Stream - RGB565</title>\n"
    "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "    <style>\n"
    "        body {\n"
    "            font-family: Arial, Helvetica, sans-serif;\n"
    "            background-color: #181818;\n"
    "            color: #fff;\n"
    "            text-align: center;\n"
    "            margin: 0;\n"
    "            padding: 20px;\n"
    "        }\n"
    "        h1 {\n"
    "            color: #4CAF50;\n"
    "            margin-bottom: 20px;\n"
    "        }\n"
    "        img {\n"
    "            max-width: 100%;\n"
    "            height: auto;\n"
    "            border: 3px solid #4CAF50;\n"
    "            border-radius: 8px;\n"
    "            box-shadow: 0 4px 8px rgba(0,0,0,0.3);\n"
    "        }\n"
    "        .container {\n"
    "            max-width: 1200px;\n"
    "            margin: 0 auto;\n"
    "        }\n"
    "        .info {\n"
    "            margin-top: 20px;\n"
    "            padding: 10px;\n"
    "            background-color: #282828;\n"
    "            border-radius: 5px;\n"
    "        }\n"
    "        .badge {\n"
    "            display: inline-block;\n"
    "            padding: 5px 10px;\n"
    "            margin: 5px;\n"
    "            background-color: #4CAF50;\n"
    "            border-radius: 3px;\n"
    "            font-weight: bold;\n"
    "        }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <div class=\"container\">\n"
    "        <h1>ESP32-S3 Camera Live Stream</h1>\n"
    "        <img id=\"stream\" src=\"/stream\" alt=\"Camera Stream\">\n"
    "        <div class=\"info\">\n"
    "            <p>Streaming from ESP32-S3 with OV2640 camera</p>\n"
    "            <div>\n"
    "                <span class=\"badge\">Format: RGB565</span>\n"
    "                <span class=\"badge\">Resolution: VGA (640x480)</span>\n"
    "                <span class=\"badge\">Converted to JPEG</span>\n"
    "            </div>\n"
    "        </div>\n"
    "    </div>\n"
    "</body>\n"
    "</html>\n";

// Content type for MJPEG stream
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

/**
 * Handler for the root page
 */
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, STREAM_HTML, HTTPD_RESP_USE_STRLEN);
}

/**
 * Handler for MJPEG stream (supports RGB565 con detección de objetos)
 */
static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t jpg_buf_len = 0;
    uint8_t *jpg_buf = NULL;
    char part_buf[128];
    bool needs_free = false;
    detection_result_t detection;

    ESP_LOGI(TAG, "Stream requested");

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set response type");
        return res;
    }

    while (true)
    {
        fb = camera_capture();
        if (!fb)
        {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        // Realizar detección si el formato es RGB565
        if (fb->format == PIXFORMAT_RGB565)
        {
            detect_object_by_color((uint16_t *)fb->buf, fb->width, fb->height,
                                   current_color_range, NULL, &detection);

            if (detection.detected)
            {
                ESP_LOGI(TAG, "Object detected! Centroid: (%d, %d), Pixels: %lu",
                         detection.centroid_x, detection.centroid_y, detection.pixel_count);
            }
        }

        // Handle different pixel formats
        if (fb->format == PIXFORMAT_JPEG)
        {
            jpg_buf = fb->buf;
            jpg_buf_len = fb->len;
            needs_free = false;
        }
        else if (fb->format == PIXFORMAT_RGB565)
        {
            // Convert RGB565 to JPEG
            if (!rgb565_to_jpeg(fb, &jpg_buf, &jpg_buf_len, 80))
            {
                ESP_LOGE(TAG, "Failed to convert RGB565 to JPEG");
                camera_fb_return(fb);
                res = ESP_FAIL;
                break;
            }
            needs_free = true;
        }
        else
        {
            ESP_LOGE(TAG, "Unsupported pixel format: %d", fb->format);
            camera_fb_return(fb);
            res = ESP_FAIL;
            break;
        }

        // Send boundary
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }

        // Send content type and length
        if (res == ESP_OK)
        {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, jpg_buf_len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }

        // Send image data
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_buf_len);
        }

        // Cleanup
        if (needs_free && jpg_buf)
        {
            free(jpg_buf);
            jpg_buf = NULL;
        }
        camera_fb_return(fb);
        fb = NULL;

        if (res != ESP_OK)
        {
            ESP_LOGW(TAG, "Stream interrupted");
            break;
        }
    }

    if (fb)
    {
        camera_fb_return(fb);
    }
    if (needs_free && jpg_buf)
    {
        free(jpg_buf);
    }

    ESP_LOGI(TAG, "Stream ended");
    return res;
}

/**
 * Handler for capture single frame (supports RGB565)
 */
static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    uint8_t *jpg_buf = NULL;
    size_t jpg_buf_len = 0;
    bool needs_free = false;

    fb = camera_capture();
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Handle different pixel formats
    if (fb->format == PIXFORMAT_JPEG)
    {
        jpg_buf = fb->buf;
        jpg_buf_len = fb->len;
        needs_free = false;
    }
    else if (fb->format == PIXFORMAT_RGB565)
    {
        // Convert RGB565 to JPEG
        if (!rgb565_to_jpeg(fb, &jpg_buf, &jpg_buf_len, 90))
        {
            ESP_LOGE(TAG, "Failed to convert RGB565 to JPEG");
            camera_fb_return(fb);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        needs_free = true;
    }
    else
    {
        ESP_LOGE(TAG, "Unsupported pixel format for capture");
        camera_fb_return(fb);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    res = httpd_resp_send(req, (const char *)jpg_buf, jpg_buf_len);

    // Cleanup
    if (needs_free && jpg_buf)
    {
        free(jpg_buf);
    }
    camera_fb_return(fb);

    return res;
}

/**
 * Handler para obtener datos de detección en formato JSON
 */
static esp_err_t detection_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    detection_result_t detection;
    char json_response[200];

    fb = camera_capture();
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Realizar detección solo en RGB565
    if (fb->format == PIXFORMAT_RGB565)
    {
        detect_object_by_color((uint16_t *)fb->buf, fb->width, fb->height,
                               current_color_range, NULL, &detection);
    }
    else
    {
        detection.detected = false;
    }

    camera_fb_return(fb);

    // Crear respuesta JSON
    snprintf(json_response, sizeof(json_response),
             "{\"detected\":%s,\"x\":%d,\"y\":%d,\"pixels\":%lu}",
             detection.detected ? "true" : "false",
             detection.centroid_x,
             detection.centroid_y,
             detection.pixel_count);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t webserver_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 8;
    config.max_resp_headers = 8;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    // URI handler for root page
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    // URI handler for stream
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    // URI handler for single capture
    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL};

    // URI handler for detection data
    httpd_uri_t detection_uri = {
        .uri = "/detection",
        .method = HTTP_GET,
        .handler = detection_handler,
        .user_ctx = NULL};

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &stream_uri);
        httpd_register_uri_handler(server, &capture_uri);
        httpd_register_uri_handler(server, &detection_uri);
        ESP_LOGI(TAG, "Web server started successfully");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Error starting web server!");
    return ESP_FAIL;
}

esp_err_t webserver_stop(void)
{
    if (server)
    {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
        return ESP_OK;
    }
    return ESP_FAIL;
}
