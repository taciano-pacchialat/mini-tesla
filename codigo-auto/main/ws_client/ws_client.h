/**
 * @file ws_client.h
 * @brief WebSocket Client for bidirectional communication with ESP32-S3 server
 * 
 * Establishes persistent WebSocket connection for:
 * - Receiving telemetry data (JSON text frames)
 * - Receiving video frames (binary frames)
 * - Sending vehicle status updates (JSON text frames)
 */

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WS_SERVER_URI "ws://192.168.4.1/ws"

// Maximum sizes
#define WS_MAX_PAYLOAD_SIZE     8192
#define WS_TX_BUFFER_SIZE       512
#define WS_RECONNECT_TIMEOUT_MS 5000

/**
 * @brief Telemetry data structure received from server
 */
typedef struct {
    char object_type[32];      // "obstacle" or "target"
    int pixel_x;               // X coordinate in image
    int pixel_y;               // Y coordinate in image
    float world_x;             // Real-world X (cm)
    float world_y;             // Real-world Y (cm)
    float distance_cm;         // Euclidean distance
    float angle_deg;           // Angle relative to vehicle
    int pixel_count;           // Area of detected object
    bool detected;             // Detection flag
    uint64_t timestamp_ms;     // Server timestamp
} telemetry_data_t;

/**
 * @brief Vehicle status structure to send to server
 */
typedef struct {
    char vehicle_id[32];       // Vehicle identifier
    int motor_left;            // Left motor speed (-255 to 255)
    int motor_right;           // Right motor speed (-255 to 255)
    int battery_mv;            // Battery voltage in millivolts
    char status[32];           // "moving", "stopped", "searching", etc.
} vehicle_status_t;

/**
 * @brief Callback type for telemetry data reception
 * 
 * @param data Pointer to received telemetry data
 */
typedef void (*telemetry_callback_t)(const telemetry_data_t *data);

/**
 * @brief Initialize WebSocket client
 * 
 * Sets up the WebSocket client and registers callback.
 * Must be called after WiFi connection is established.
 * 
 * @param callback Function to call when telemetry is received
 * @return ESP_OK on success
 */
esp_err_t ws_client_init(telemetry_callback_t callback);

/**
 * @brief Connect to WebSocket server
 * 
 * Establishes WebSocket connection with auto-reconnect enabled.
 * Non-blocking call.
 * 
 * @return ESP_OK on success
 */
esp_err_t ws_client_connect(void);

/**
 * @brief Send vehicle status to server
 * 
 * Sends status as JSON text frame over WebSocket.
 * Non-blocking, queues data if connection busy.
 * 
 * @param status Pointer to vehicle status structure
 * @return ESP_OK if queued successfully
 */
esp_err_t ws_client_send_status(const vehicle_status_t *status);

/**
 * @brief Check if WebSocket is connected
 * 
 * @return true if connected, false otherwise
 */
bool ws_client_is_connected(void);

/**
 * @brief Disconnect and cleanup WebSocket client
 * 
 * @return ESP_OK on success
 */
esp_err_t ws_client_disconnect(void);

/**
 * @brief Get last received telemetry data
 * 
 * Thread-safe copy of most recent telemetry.
 * 
 * @param data Pointer to buffer for telemetry data
 * @return ESP_OK if valid data available, ESP_FAIL otherwise
 */
esp_err_t ws_client_get_last_telemetry(telemetry_data_t *data);

#ifdef __cplusplus
}
#endif

#endif // WS_CLIENT_H
