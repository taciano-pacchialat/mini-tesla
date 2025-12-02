/*
 * ESP32-S3 Camera Streaming Application
 * Captures video from OV2640 camera and streams it via HTTP
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Application modules
#include "wifi/wifi.h"
#include "camera_driver/camera_driver.h"
#include "webserver/webserver.h"
#include "test_detection.h"
#include "test_homography.h"

// Configuración de prueba - cambiar a 1 para habilitar tests
#define ENABLE_DETECTION_TEST 0
#define ENABLE_HOMOGRAPHY_TEST 1

// For opencv compatibility
#undef EPS

static const char *TAG = "Main";

void app_main(void)
{
    esp_err_t ret;
    char ip_address[16] = {0};

    ESP_LOGI(TAG, "=== ESP32-S3 Camera Streaming Application ===");

    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    ret = wifi_init_sta();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi initialization failed!");
        return;
    }

    // Get and display IP address
    if (wifi_get_ip_address(ip_address, sizeof(ip_address)) == ESP_OK)
    {
        ESP_LOGI(TAG, "WiFi connected! IP address: %s", ip_address);
        ESP_LOGI(TAG, "Open your browser and navigate to: http://%s", ip_address);
    }

    // Initialize Camera
    ESP_LOGI(TAG, "Initializing camera...");
    ret = camera_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "Camera initialized successfully");

    // Start Web Server
    ESP_LOGI(TAG, "Starting web server...");
    ret = webserver_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Web server failed to start!");
        return;
    }

    ESP_LOGI(TAG, "=== System ready! ===");
    ESP_LOGI(TAG, "Stream URL: http://%s/stream", ip_address);
    ESP_LOGI(TAG, "Capture URL: http://%s/capture", ip_address);
    ESP_LOGI(TAG, "Detection API: http://%s/detection", ip_address);

#if ENABLE_DETECTION_TEST
    // Ejecutar test de detección una vez
    ESP_LOGI(TAG, "Ejecutando test de detección...");
    vTaskDelay(pdMS_TO_TICKS(2000)); // Esperar 2s para estabilidad
    test_object_detection();
#endif

#if ENABLE_HOMOGRAPHY_TEST
    // Ejecutar test de homografía y distancia
    ESP_LOGI(TAG, "Ejecutando tests de homografía...");
    vTaskDelay(pdMS_TO_TICKS(2000)); // Esperar 2s para estabilidad
    run_homography_tests();
#endif

    // Keep the application running
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
