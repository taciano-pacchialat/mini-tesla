#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

// WiFi Configuration - Modifica estos valores con tus credenciales
#define WIFI_SSID "Personal-140-2.4GHz"
#define WIFI_PASSWORD "00417225972"

// WiFi connection timeout in milliseconds
#define WIFI_MAXIMUM_RETRY 5

/**
 * @brief Initialize WiFi in station mode and connect to configured AP
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_init_sta(void);

/**
 * @brief Get the IP address assigned to the station
 *
 * @param ip_str Buffer to store IP address string (minimum 16 bytes)
 * @param len Length of the buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_get_ip_address(char *ip_str, size_t len);

#endif // WIFI_H
