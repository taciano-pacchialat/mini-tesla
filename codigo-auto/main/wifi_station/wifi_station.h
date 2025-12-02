/**
 * @file wifi_station.h
 * @brief WiFi Station component for ESP32-CAM autonomous vehicle
 *
 * Connects as WiFi client to the ESP32-S3 SoftAP server.
 * Uses native ESP-IDF esp_wifi API for robust connection management.
 */

#ifndef WIFI_STATION_H
#define WIFI_STATION_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C"
{
#endif

// WiFi credentials for connecting to ESP32-S3 SoftAP
#define WIFI_SSID "ESP32-Vision-Bot"
#define WIFI_PASSWORD "vision2025"
#define WIFI_SERVER_IP "192.168.4.1"

// Connection parameters
#define WIFI_MAX_RETRY 10
#define WIFI_RETRY_DELAY_MS 5000

    /**
     * @brief Initialize WiFi Station mode
     *
     * Initializes NVS, netif, event loop, and WiFi driver.
     * Must be called before wifi_station_connect().
     *
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t wifi_station_init(void);

    /**
     * @brief Connect to the ESP32-S3 SoftAP
     *
     * Starts WiFi in station mode and attempts connection.
     * Blocks until connected or max retries reached.
     *
     * @return ESP_OK on success, ESP_FAIL if connection fails
     */
    esp_err_t wifi_station_connect(void);

    /**
     * @brief Check if WiFi is currently connected
     *
     * @return true if connected to AP, false otherwise
     */
    bool wifi_station_is_connected(void);

    /**
     * @brief Disconnect from AP and deinitialize WiFi
     *
     * @return ESP_OK on success
     */
    esp_err_t wifi_station_disconnect(void);

    /**
     * @brief Get assigned IP address as string
     *
     * @param ip_str Buffer to store IP string (min 16 bytes)
     * @return ESP_OK on success, ESP_FAIL if not connected
     */
    esp_err_t wifi_station_get_ip(char *ip_str);

#ifdef __cplusplus
}
#endif

#endif // WIFI_STATION_H
