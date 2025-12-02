/**
 * @file main.c
 * @brief ESP32-CAM Autonomous Vehicle - Main Entry Point
 *
 * Architecture:
 * - Core 0: WiFi, WebSocket, Status Transmission
 * - Core 1: Vision Processing, Autonomous Control
 *
 * Components:
 * - WiFi Station: Connects to ESP32-S3 SoftAP
 * - WebSocket Client: Bidirectional communication
 * - Motor Control: MCPWM-based differential drive
 * - Autonomous Task: Reactive control logic with local veto
 * - Vision Engine: Local obstacle detection (green objects)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"

// Component headers
#include "wifi_station/wifi_station.h"
#include "ws_client/ws_client.h"
#include "motor_control/motor_control.h"
#include "autonomous_task/autonomous_task.h"
#include "vision_engine/vision_engine.h"

static const char *TAG = "[Main]";

// FreeRTOS handles
static QueueHandle_t telemetry_queue = NULL;
static EventGroupHandle_t system_events = NULL;

// Event bits
#define WIFI_CONNECTED_BIT BIT0
#define WEBSOCKET_CONNECTED_BIT BIT1
#define EMERGENCY_STOP_BIT BIT2

// Vehicle configuration
#define VEHICLE_ID "ESP32CAM_01"

// Task stack sizes
#define STACK_SIZE_WIFI 4096
#define STACK_SIZE_WS_TX 3072
#define STACK_SIZE_CONTROL 4096
#define STACK_SIZE_MONITOR 2048

// Battery monitoring (optional - using ADC)
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0 // GPIO36 on ESP32
#define BATTERY_VOLTAGE_DIVIDER 2.0f      // Adjust based on your circuit

/**
 * @brief Telemetry callback - called when data arrives from WebSocket
 */
static void telemetry_received_callback(const telemetry_data_t *data)
{
    if (telemetry_queue != NULL)
    {
        // Send to control task queue (non-blocking)
        if (xQueueSend(telemetry_queue, data, 0) != pdTRUE)
        {
            ESP_LOGW(TAG, "Telemetry queue full, dropping data");
        }
    }
}

/**
 * @brief Task: Autonomous Control Loop with Vision Fusion
 * Runs on Core 1 (Application CPU)
 * Priority: 6 (High)
 *
 * Implements fusion logic:
 * - Reads remote telemetry from WebSocket
 * - Reads local vision veto flag
 * - If local veto active: BLOCK forward motion (safety override)
 * - Otherwise: Execute remote telemetry commands
 */
static void control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Control task started on core %d", xPortGetCoreID());

    telemetry_data_t telemetry;
    TickType_t last_telemetry_time = xTaskGetTickCount();
    const TickType_t telemetry_timeout = pdMS_TO_TICKS(2000); // 2 seconds

    while (1)
    {
        // Check local vision veto status
        bool local_veto = vision_engine_is_veto_active();

        // Wait for remote telemetry data from queue
        if (xQueueReceive(telemetry_queue, &telemetry, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            last_telemetry_time = xTaskGetTickCount();

            // FUSION LOGIC: Process telemetry with local veto override
            autonomous_process_with_veto(&telemetry, local_veto);

            if (local_veto)
            {
                ESP_LOGW(TAG, "⚠️  VETO: Local camera blocked forward motion");
            }
        }
        else
        {
            // No remote telemetry received
            // Still check local veto for safety
            if (local_veto)
            {
                ESP_LOGW(TAG, "Local veto active - motors stopped");
                motor_emergency_stop();
            }

            // Check if we haven't received telemetry for too long
            if ((xTaskGetTickCount() - last_telemetry_time) > telemetry_timeout)
            {
                ESP_LOGW(TAG, "Telemetry timeout - no data for 2 seconds");
                // Emergency stop if connection seems lost
                if (!ws_client_is_connected())
                {
                    ESP_LOGE(TAG, "WebSocket disconnected - EMERGENCY STOP");
                    autonomous_emergency_stop();
                    xEventGroupSetBits(system_events, EMERGENCY_STOP_BIT);
                }
            }
        }

        // Small delay to prevent task starvation
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Task: Status Transmission
 * Runs on Core 0 (Protocol CPU)
 * Priority: 4
 */
static void status_tx_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Status transmission task started on core %d", xPortGetCoreID());

    vehicle_status_t status;
    strncpy(status.vehicle_id, VEHICLE_ID, sizeof(status.vehicle_id) - 1);
    status.vehicle_id[sizeof(status.vehicle_id) - 1] = '\0';

    while (1)
    {
        // Wait for WebSocket connection
        EventBits_t bits = xEventGroupWaitBits(system_events,
                                               WEBSOCKET_CONNECTED_BIT,
                                               pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(1000));

        if (bits & WEBSOCKET_CONNECTED_BIT)
        {
            // Get current motor speeds using the correct API
            motor_get_speeds(&status.motor_left, &status.motor_right);

            // Get battery voltage (simplified - using fixed value)
            // In real implementation, read from ADC
            status.battery_mv = 3700; // Placeholder

            // Get control state as status string
            control_state_t state = autonomous_get_state();
            strncpy(status.status, autonomous_state_to_string(state), sizeof(status.status) - 1);
            status.status[sizeof(status.status) - 1] = '\0';

            // Send status to server
            esp_err_t err = ws_client_send_status(&status);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to send status");
            }
        }

        // Send status every 100ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Task: Battery/System Monitor
 * Runs on Core 1 (Application CPU)
 * Priority: 3 (Low)
 */
static void monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Monitor task started on core %d", xPortGetCoreID());

    while (1)
    {
        // Get motor speeds for logging
        int left_speed, right_speed;
        motor_get_speeds(&left_speed, &right_speed);

        // Log system status periodically
        ESP_LOGI(TAG, "Status: WiFi=%s, WebSocket=%s, State=%s, Motors=[L:%d, R:%d]",
                 wifi_station_is_connected() ? "OK" : "DISCONNECTED",
                 ws_client_is_connected() ? "OK" : "DISCONNECTED",
                 autonomous_state_to_string(autonomous_get_state()),
                 left_speed, right_speed);

        // Check for emergency conditions
        if (!wifi_station_is_connected())
        {
            ESP_LOGW(TAG, "WiFi disconnected - triggering emergency stop");
            autonomous_emergency_stop();
            xEventGroupSetBits(system_events, EMERGENCY_STOP_BIT);
        }

        // Monitor every 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "ESP32-CAM Autonomous Vehicle Client");
    ESP_LOGI(TAG, "Vehicle ID: %s", VEHICLE_ID);
    ESP_LOGI(TAG, "====================================");

    // Create event group
    system_events = xEventGroupCreate();
    if (system_events == NULL)
    {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }

    // Create telemetry queue
    telemetry_queue = xQueueCreate(10, sizeof(telemetry_data_t));
    if (telemetry_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create telemetry queue");
        return;
    }

    // Initialize motor control
    ESP_LOGI(TAG, "Initializing motor control...");
    if (motor_control_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize motor control");
        return;
    }

    // Initialize vision engine (local camera-based obstacle detection)
    ESP_LOGI(TAG, "Initializing vision engine...");
    if (vision_engine_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize vision engine");
        ESP_LOGW(TAG, "Continuing without local vision (veto disabled)");
        // Don't return - can operate without local vision, just without veto
    }
    else
    {
        ESP_LOGI(TAG, "Starting vision processing task...");
        if (vision_engine_start() != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start vision task");
        }
        else
        {
            ESP_LOGI(TAG, "Vision engine running on Core 1");
        }
    }

    // Initialize autonomous control
    ESP_LOGI(TAG, "Initializing autonomous control...");
    if (autonomous_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize autonomous control");
        return;
    }

    // Initialize WiFi Station
    ESP_LOGI(TAG, "Initializing WiFi Station...");
    if (wifi_station_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
        return;
    }

    // Connect to WiFi
    ESP_LOGI(TAG, "Connecting to WiFi...");
    if (wifi_station_connect() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        autonomous_emergency_stop();
        return;
    }

    xEventGroupSetBits(system_events, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "WiFi connected successfully");

    // Initialize WebSocket client
    ESP_LOGI(TAG, "Initializing WebSocket client...");
    if (ws_client_init(telemetry_received_callback) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        return;
    }

    // Connect to WebSocket
    ESP_LOGI(TAG, "Connecting to WebSocket server...");
    if (ws_client_connect() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect to WebSocket");
        return;
    }

    // Give WebSocket time to establish connection
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (ws_client_is_connected())
    {
        xEventGroupSetBits(system_events, WEBSOCKET_CONNECTED_BIT);
        ESP_LOGI(TAG, "WebSocket connected successfully");
    }

    // Create FreeRTOS tasks with core affinity
    ESP_LOGI(TAG, "Creating FreeRTOS tasks...");

    // Control task on Core 1 (Application CPU)
    BaseType_t ret = xTaskCreatePinnedToCore(
        control_task,
        "control_task",
        STACK_SIZE_CONTROL,
        NULL,
        6, // High priority
        NULL,
        1 // Core 1
    );
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create control task");
        return;
    }

    // Status transmission task on Core 0 (Protocol CPU)
    ret = xTaskCreatePinnedToCore(
        status_tx_task,
        "status_tx_task",
        STACK_SIZE_WS_TX,
        NULL,
        4, // Medium priority
        NULL,
        0 // Core 0
    );
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create status transmission task");
        return;
    }

    // Monitor task on Core 1
    ret = xTaskCreatePinnedToCore(
        monitor_task,
        "monitor_task",
        STACK_SIZE_MONITOR,
        NULL,
        3, // Low priority
        NULL,
        1 // Core 1
    );
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create monitor task");
        return;
    }

    ESP_LOGI(TAG, "System initialization complete - entering autonomous mode");
    ESP_LOGI(TAG, "Ready to receive telemetry and control motors");
}