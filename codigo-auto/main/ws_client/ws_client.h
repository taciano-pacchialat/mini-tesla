/**
 * @file ws_client.h
 * @brief WebSocket Client for bidirectional communication with ESP32-S3 server
 *
 * Establishes persistent WebSocket connection for:
 * - Receiving manual control commands + stream status (JSON text frames)
 * - Sending JPEG video frames (binary frames)
 * - Sending vehicle status updates (JSON text frames)
 */

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define WS_SERVER_URI "ws://192.168.4.1/ws"

// Maximum sizes
#define WS_MAX_PAYLOAD_SIZE 32768
#define WS_TX_BUFFER_SIZE 512
#define WS_RECONNECT_TIMEOUT_MS 5000

    /**
     * @brief Manual control commands supported by the dashboard
     */
    typedef enum
    {
        CONTROL_CMD_STOP = 0,
        CONTROL_CMD_FORWARD,
        CONTROL_CMD_BACKWARD,
        CONTROL_CMD_LEFT,
        CONTROL_CMD_RIGHT,
    } control_command_t;

    typedef struct
    {
        control_command_t command;
        uint64_t timestamp_ms;
        char raw_command[16];
    } control_message_t;

    /**
     * @brief Vehicle status structure to send to server
     */
    typedef struct
    {
        char vehicle_id[32]; // Vehicle identifier
        int motor_left;      // Left motor speed (-255 to 255)
        int motor_right;     // Right motor speed (-255 to 255)
        int battery_mv;      // Battery voltage in millivolts
        char status[32];     // "moving", "stopped", "searching", etc.
    } vehicle_status_t;

    /**
     * @brief Callback invoked when a control message arrives from the server
     */
    typedef void (*control_callback_t)(const control_message_t *message);

    /**
     * @brief Initialize WebSocket client
     *
     * Sets up the WebSocket client and registers callback.
     * Must be called after WiFi connection is established.
     *
     * @param vehicle_id Null-terminated ID reported to the server
     * @param callback Function to call when manual control arrives
     * @return ESP_OK on success
     */
    esp_err_t ws_client_init(const char *vehicle_id, control_callback_t callback);

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
     * @brief Send a JPEG frame to the server
     *
     * Uses WebSocket binary frames so the ESP32-S3 pueda reenviar el video.
     *
     * @param frame Pointer to JPEG buffer
     * @param length Buffer length in bytes
     * @return ESP_OK if the frame was queued for transmission
     */
    esp_err_t ws_client_send_frame(const uint8_t *frame, size_t length);

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
     * @brief Enable/disable streaming of video frames
     *
     * @return true if streaming is enabled, false otherwise
     */
    bool ws_client_stream_enabled(void);

#ifdef __cplusplus
}
#endif

#endif // WS_CLIENT_H
