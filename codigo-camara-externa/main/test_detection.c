#include "vision/vision.h"
#include "camera_driver/camera_driver.h"
#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "TestDetection";

/**
 * Función de prueba para verificar la detección de objetos
 * Puedes llamar esta función desde main.c para testear
 */
void test_object_detection(void)
{
    ESP_LOGI(TAG, "Iniciando test de detección de objetos...");

    // Capturar imagen
    camera_fb_t *fb = camera_capture();
    if (!fb)
    {
        ESP_LOGE(TAG, "Error al capturar imagen");
        return;
    }

    ESP_LOGI(TAG, "Imagen capturada: %dx%d, formato: %d", fb->width, fb->height, fb->format);

    if (fb->format != PIXFORMAT_RGB565)
    {
        ESP_LOGE(TAG, "Formato no es RGB565, test cancelado");
        camera_fb_return(fb);
        return;
    }

    // Probar detección con diferentes colores
    const color_range_t *colors[] = {&COLOR_RED, &COLOR_GREEN, &COLOR_BLUE, &COLOR_YELLOW};
    const char *color_names[] = {"ROJO", "VERDE", "AZUL", "AMARILLO"};

    for (int i = 0; i < 4; i++)
    {
        detection_result_t result;

        ESP_LOGI(TAG, "Probando detección de color: %s", color_names[i]);
        detect_object_by_color((uint16_t *)fb->buf, fb->width, fb->height, colors[i], NULL, &result);

        if (result.detected)
        {
            ESP_LOGI(TAG, "✓ %s detectado!", color_names[i]);
            ESP_LOGI(TAG, "  Centroide: (%d, %d)", result.centroid_x, result.centroid_y);
            ESP_LOGI(TAG, "  Píxeles: %lu", result.pixel_count);
            ESP_LOGI(TAG, "  Cobertura: %.2f%%", (result.pixel_count * 100.0) / (fb->width * fb->height));
        }
        else
        {
            ESP_LOGI(TAG, "✗ %s no detectado", color_names[i]);
        }
    }

    camera_fb_return(fb);
    ESP_LOGI(TAG, "Test de detección completado");
}

/**
 * Tarea continua de monitoreo de detección
 * Puedes ejecutar esto en una tarea separada para debugging
 */
void detection_monitor_task(void *pvParameters)
{
    const color_range_t *target_color = (const color_range_t *)pvParameters;
    detection_result_t result;

    while (1)
    {
        camera_fb_t *fb = camera_capture();
        if (fb && fb->format == PIXFORMAT_RGB565)
        {
            detect_object_by_color((uint16_t *)fb->buf, fb->width, fb->height, target_color, NULL, &result);

            if (result.detected)
            {
                ESP_LOGI(TAG, "Monitor: Objeto en (%d, %d), %lu px",
                         result.centroid_x, result.centroid_y, result.pixel_count);
            }

            camera_fb_return(fb);
        }

        vTaskDelay(pdMS_TO_TICKS(500)); // Revisar cada 500ms
    }
}
