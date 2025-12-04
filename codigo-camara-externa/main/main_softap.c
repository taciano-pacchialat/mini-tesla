/*
 * ESP32-S3 Vision System with Multi-Core Architecture
 *
 * ARQUITECTURA:
 * - Core 0 (Protocol CPU): WiFi, LwIP, HTTP Server, WebSocket
 * - Core 1 (Application CPU): Vision Processing, Object Detection
 *
 * COMUNICACIÓN:
 * - SoftAP: 192.168.4.1 (ESP32-Vision-Bot)
 * - WebSocket: Telemetría JSON + Video JPEG
 * - Asíncrono: httpd_ws_send_frame_async
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Módulos del sistema
#include "softap/softap.h"
#include "camera_driver/camera_driver.h"
#include "ws_server/ws_server.h"
#include "vision_task/vision_task.h"

// Tests opcionales
#include "test_detection.h"
#include "test_homography.h"

// Configuración de modo de operación
#define ENABLE_DETECTION_TEST 0
#define ENABLE_HOMOGRAPHY_TEST 0

// For opencv compatibility
#undef EPS

static const char *TAG = "Main";

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "╔════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ESP32-S3 Vision System - Multi-Core v2.0     ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ Core 0: WiFi + HTTP + WebSocket                ║");
    ESP_LOGI(TAG, "║ Core 1: Vision Processing                      ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════╝");

    // ========== INICIALIZACIÓN DE RED ==========
    ESP_LOGI(TAG, "[1/4] Inicializando SoftAP...");
    ret = softap_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error inicializando SoftAP");
        return;
    }
    ESP_LOGI(TAG, "✓ SoftAP iniciado: ESP32-Vision-Bot @ 192.168.4.1");

    // ========== INICIALIZACIÓN DE CÁMARA ==========
    ESP_LOGI(TAG, "[2/4] Inicializando cámara OV2640...");
    ret = camera_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error inicializando cámara");
        return;
    }
    ESP_LOGI(TAG, "✓ Cámara inicializada: RGB565 @ VGA (640x480)");

    // ========== INICIALIZACIÓN DE SERVIDOR WEBSOCKET ==========
    ESP_LOGI(TAG, "[3/4] Iniciando servidor WebSocket...");
    ret = ws_server_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error iniciando servidor WebSocket");
        return;
    }
    ESP_LOGI(TAG, "✓ Servidor WebSocket activo en Core 0");

    // ========== INICIALIZACIÓN DE TAREA DE VISIÓN ==========
    ESP_LOGI(TAG, "[4/4] Iniciando tarea de visión en Core 1...");
    ret = vision_task_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error iniciando tarea de visión");
        return;
    }
    ESP_LOGI(TAG, "✓ Tarea de visión activa en Core 1");

    // ========== SISTEMA LISTO ==========
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          SISTEMA COMPLETAMENTE ACTIVO          ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ 1. Conectar WiFi a: ESP32-Vision-Bot           ║");
    ESP_LOGI(TAG, "║ 2. Contraseña: 12345678                        ║");
    ESP_LOGI(TAG, "║ 3. Abrir: http://192.168.4.1                   ║");
    ESP_LOGI(TAG, "║                                                ║");
    ESP_LOGI(TAG, "║ WebSocket:                                     ║");
    ESP_LOGI(TAG, "║   - Telemetría: JSON (texto)                   ║");
    ESP_LOGI(TAG, "║   - Video: JPEG (binario)                      ║");
    ESP_LOGI(TAG, "║   - Comunicación: Full-duplex asíncrona        ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════╝");

    // ========== TESTS OPCIONALES ==========
#if ENABLE_DETECTION_TEST
    ESP_LOGI(TAG, "Ejecutando test de detección...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    test_object_detection();
#endif

#if ENABLE_HOMOGRAPHY_TEST
    ESP_LOGI(TAG, "Ejecutando tests de homografía...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    run_homography_tests();
#endif

    // ========== MONITOREO DEL SISTEMA ==========
    ESP_LOGI(TAG, "Iniciando monitoreo del sistema...");
    uint32_t last_report = 0;

    while (1)
    {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Reporte cada 10 segundos
        if (now - last_report >= 10000)
        {
            ESP_LOGI(TAG, "═══ Estado del Sistema ═══");
            ESP_LOGI(TAG, "  Clientes WS:    %d", ws_server_get_clients_count());
            ESP_LOGI(TAG, "  Estaciones AP:  %d", softap_get_connected_stations());
            ESP_LOGI(TAG, "  Free Heap:      %lu bytes", esp_get_free_heap_size());
            ESP_LOGI(TAG, "  Uptime:         %lu ms", now);

            last_report = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
