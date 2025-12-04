#include "vision_task.h"
#include "camera_driver/camera_driver.h"
#include "vision/vision.h"
#include "vision/homography.h"
#include "ws_server/ws_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include <string.h>
#include <math.h>

static const char *TAG = "VisionTask";

static TaskHandle_t vision_task_handle = NULL;
static QueueHandle_t frame_queue = NULL;
static const color_range_t *current_color = &COLOR_RED;
static bool processing_enabled = true;
static homography_matrix_t h_matrix;

/**
 * @brief Convierte camera_fb_t a JPEG
 */
static bool frame_to_jpeg(camera_fb_t *fb, uint8_t **jpeg_data, size_t *jpeg_len)
{
    // Usar la función de esp32-camera para convertir frame a JPEG
    return frame2jpg(fb, 80, jpeg_data, jpeg_len);
}

/**
 * @brief Tarea principal de visión (Core 1)
 */
static void vision_task_function(void *pvParameters)
{
    ESP_LOGI(TAG, "Tarea de visión iniciada en Core %d", xPortGetCoreID());

    // Inicializar matriz de homografía
    homography_load_default(&h_matrix, 640, 480, 100.0f, 80.0f);

    uint32_t frame_count = 0;
    uint32_t last_fps_time = esp_timer_get_time() / 1000;

    while (1)
    {
        // Capturar frame
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb)
        {
            ESP_LOGE(TAG, "Error capturando frame");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        detection_result_t detection = {0};

        // Procesar detección si está habilitado
        if (processing_enabled && fb->format == PIXFORMAT_RGB565)
        {
            // Detectar objeto con transformación homográfica
            detect_object_by_color(
                (uint16_t *)fb->buf,
                fb->width,
                fb->height,
                current_color,
                &h_matrix,
                &detection);
        }

        // Convertir frame a JPEG si hay clientes conectados
        if (ws_server_has_clients())
        {
            uint8_t *jpeg_data = NULL;
            size_t jpeg_len = 0;

            if (frame_to_jpeg(fb, &jpeg_data, &jpeg_len))
            {
                // Enviar frame por WebSocket (asíncrono)
                ws_server_send_video_frame(FRAME_SOURCE_ESP32S3, jpeg_data, jpeg_len);
                free(jpeg_data);
            }
        }

        // Liberar frame buffer
        esp_camera_fb_return(fb);

        // Calcular FPS
        frame_count++;
        uint32_t now = esp_timer_get_time() / 1000;
        if (now - last_fps_time >= 1000)
        {
            ESP_LOGI(TAG, "FPS: %lu | Clientes WS: %d | Detección: %s",
                     frame_count,
                     ws_server_get_clients_count(),
                     detection.detected ? "SI" : "NO");
            frame_count = 0;
            last_fps_time = now;
        }

        // Pequeña pausa para no saturar
        vTaskDelay(pdMS_TO_TICKS(33)); // ~30 FPS máximo
    }

    vTaskDelete(NULL);
}

esp_err_t vision_task_start(void)
{
    if (vision_task_handle != NULL)
    {
        ESP_LOGW(TAG, "Tarea de visión ya está corriendo");
        return ESP_OK;
    }

    // Crear cola para frames procesados
    frame_queue = xQueueCreate(VISION_QUEUE_SIZE, sizeof(processed_frame_t));
    if (frame_queue == NULL)
    {
        ESP_LOGE(TAG, "Error creando cola de frames");
        return ESP_FAIL;
    }

    // Crear tarea en Core 1 (Application CPU)
    BaseType_t result = xTaskCreatePinnedToCore(
        vision_task_function,
        "vision_task",
        VISION_TASK_STACK_SIZE,
        NULL,
        VISION_TASK_PRIORITY,
        &vision_task_handle,
        VISION_TASK_CORE_ID);

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Error creando tarea de visión");
        vQueueDelete(frame_queue);
        frame_queue = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "╔════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║       Tarea de Visión Iniciada                 ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ Core Affinity:  Core 1 (Application CPU)       ║");
    ESP_LOGI(TAG, "║ Prioridad:      %d                              ║", VISION_TASK_PRIORITY);
    ESP_LOGI(TAG, "║ Stack Size:     %d bytes                       ║", VISION_TASK_STACK_SIZE);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════╝");

    return ESP_OK;
}

esp_err_t vision_task_stop(void)
{
    if (vision_task_handle)
    {
        vTaskDelete(vision_task_handle);
        vision_task_handle = NULL;
    }

    if (frame_queue)
    {
        vQueueDelete(frame_queue);
        frame_queue = NULL;
    }

    ESP_LOGI(TAG, "Tarea de visión detenida");
    return ESP_OK;
}

QueueHandle_t vision_task_get_queue(void)
{
    return frame_queue;
}

esp_err_t vision_task_set_target_color(const char *color_name)
{
    if (strcmp(color_name, "RED") == 0)
    {
        current_color = &COLOR_RED;
    }
    else if (strcmp(color_name, "GREEN") == 0)
    {
        current_color = &COLOR_GREEN;
    }
    else if (strcmp(color_name, "BLUE") == 0)
    {
        current_color = &COLOR_BLUE;
    }
    else if (strcmp(color_name, "YELLOW") == 0)
    {
        current_color = &COLOR_YELLOW;
    }
    else
    {
        ESP_LOGE(TAG, "Color desconocido: %s", color_name);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Color objetivo cambiado a: %s", color_name);
    return ESP_OK;
}

void vision_task_enable_processing(bool enable)
{
    processing_enabled = enable;
    ESP_LOGI(TAG, "Procesamiento de visión %s", enable ? "HABILITADO" : "DESHABILITADO");
}
