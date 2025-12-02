#include "esp_log.h"
#include "vision/homography.h"
#include "vision/vision.h"
#include "camera_driver/camera_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "TestHomography";

/**
 * @brief Prueba básica de transformación homográfica
 *
 * Carga una matriz de homografía por defecto y prueba transformaciones
 * de puntos conocidos para validar el sistema.
 */
void test_homography_basic(void)
{
    ESP_LOGI(TAG, "=== Test Homografía Básica ===");

    // Crear e inicializar matriz con valores por defecto
    homography_matrix_t h_matrix;
    // Matriz simple de calibración por defecto para área de 100x80 cm
    homography_load_default(&h_matrix, 640, 480, 100.0f, 80.0f); // Resolución VGA, área 100x80cm

    // Probar transformaciones de puntos de prueba
    pixel_point_t test_points[] = {
        {0, 0},     // Esquina superior izquierda
        {640, 0},   // Esquina superior derecha
        {0, 480},   // Esquina inferior izquierda
        {640, 480}, // Esquina inferior derecha
        {320, 240}  // Centro
    };

    for (int i = 0; i < 5; i++)
    {
        world_point_t world_pt;
        homography_transform(&h_matrix, test_points[i], &world_pt);

        ESP_LOGI(TAG, "Pixel (%d, %d) -> Mundo (%.2f cm, %.2f cm)",
                 test_points[i].u, test_points[i].v,
                 world_pt.x, world_pt.y);
    }
}

/**
 * @brief Test de detección con transformación a coordenadas reales
 *
 * Captura un frame, detecta un objeto rojo y muestra sus coordenadas
 * tanto en píxeles como en el mundo real.
 */
void test_detection_with_distance(void)
{
    ESP_LOGI(TAG, "=== Test Detección con Distancia ===");

    // Inicializar matriz de homografía
    homography_matrix_t h_matrix;
    homography_load_default(&h_matrix, 640, 480, 100.0f, 80.0f);

    ESP_LOGI(TAG, "Capturando frame...");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Error capturando frame");
        return;
    }

    ESP_LOGI(TAG, "Frame capturado: %dx%d, formato: %d, tamaño: %zu bytes",
             fb->width, fb->height, fb->format, fb->len);

    // Preparar detección de objeto rojo
    detection_result_t result;

    // Realizar detección con transformación
    detect_object_by_color(
        (uint16_t *)fb->buf,
        fb->width,
        fb->height,
        &COLOR_RED,
        &h_matrix, // Pasar matriz para obtener coordenadas reales
        &result);

    // Mostrar resultados
    if (result.detected)
    {
        ESP_LOGI(TAG, "✓ Objeto detectado:");
        ESP_LOGI(TAG, "  Píxeles: (%d, %d)", result.centroid_x, result.centroid_y);
        ESP_LOGI(TAG, "  Mundo real: (%.2f cm, %.2f cm)",
                 result.world_coords.x, result.world_coords.y);
        ESP_LOGI(TAG, "  Área: %lu píxeles", result.pixel_count);

        // Calcular distancia euclidiana desde el origen
        float distance = sqrtf(result.world_coords.x * result.world_coords.x +
                               result.world_coords.y * result.world_coords.y);
        ESP_LOGI(TAG, "  Distancia al origen: %.2f cm", distance);
    }
    else
    {
        ESP_LOGI(TAG, "✗ No se detectó objeto rojo");
    }

    esp_camera_fb_return(fb);
}

/**
 * @brief Test de diferentes colores con medición de distancia
 */
void test_multicolor_detection_with_distance(void)
{
    ESP_LOGI(TAG, "=== Test Multi-Color con Distancia ===");

    // Configurar matriz de homografía
    homography_matrix_t h_matrix;
    homography_load_default(&h_matrix, 640, 480, 100.0f, 80.0f);

    // Capturar frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Error capturando frame");
        return;
    }

    // Lista de colores a detectar
    struct
    {
        const char *name;
        const color_range_t *range;
    } colors[] = {
        {"ROJO", &COLOR_RED},
        {"VERDE", &COLOR_GREEN},
        {"AZUL", &COLOR_BLUE},
        {"AMARILLO", &COLOR_YELLOW}};

    ESP_LOGI(TAG, "Buscando objetos de diferentes colores...");

    for (int i = 0; i < 4; i++)
    {
        detection_result_t result;

        detect_object_by_color(
            (uint16_t *)fb->buf,
            fb->width,
            fb->height,
            colors[i].range,
            &h_matrix,
            &result);

        if (result.detected)
        {
            float distance = sqrtf(result.world_coords.x * result.world_coords.x +
                                   result.world_coords.y * result.world_coords.y);

            ESP_LOGI(TAG, "%s: Pixel(%d,%d) -> Mundo(%.1f,%.1f)cm, Dist=%.1f cm, %lu px",
                     colors[i].name,
                     result.centroid_x, result.centroid_y,
                     result.world_coords.x, result.world_coords.y,
                     distance,
                     result.pixel_count);
        }
        else
        {
            ESP_LOGI(TAG, "%s: No detectado", colors[i].name);
        }
    }

    esp_camera_fb_return(fb);
}

/**
 * @brief Ejecuta todos los tests de homografía
 */
void run_homography_tests(void)
{
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Tests del Sistema de Homografía y Distancia ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");

    // Test 1: Transformaciones básicas
    test_homography_basic();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 2: Detección con distancia
    test_detection_with_distance();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 3: Multi-color con distancia
    test_multicolor_detection_with_distance();

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║         Tests completados                    ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");
}
