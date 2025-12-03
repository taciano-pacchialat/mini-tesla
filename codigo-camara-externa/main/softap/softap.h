#ifndef SOFTAP_H
#define SOFTAP_H

#include "esp_err.h"

/**
 * @brief Configuración del SoftAP
 */
#define SOFTAP_SSID "ESP32-Vision-Bot"
#define SOFTAP_PASSWORD "12345678"  // Mínimo 8 caracteres para WPA2
#define SOFTAP_CHANNEL 6          // Canal 6 suele tener menos interferencia
#define SOFTAP_MAX_CONNECTIONS 4

/**
 * @brief IP estática del SoftAP
 */
#define SOFTAP_IP_ADDR "192.168.4.1"
#define SOFTAP_GATEWAY_ADDR "192.168.4.1"
#define SOFTAP_NETMASK_ADDR "255.255.255.0"

/**
 * @brief Inicializa el modo SoftAP (Punto de Acceso)
 * 
 * Crea una red WiFi propia para que otros dispositivos se conecten.
 * El ESP32-S3 actuará como servidor con IP 192.168.4.1
 * 
 * @return ESP_OK si la inicialización fue exitosa
 */
esp_err_t softap_init(void);

/**
 * @brief Detiene el SoftAP
 * 
 * @return ESP_OK si se detuvo correctamente
 */
esp_err_t softap_stop(void);

/**
 * @brief Obtiene el número de estaciones conectadas
 * 
 * @return Número de dispositivos conectados al SoftAP
 */
uint8_t softap_get_connected_stations(void);

#endif // SOFTAP_H
