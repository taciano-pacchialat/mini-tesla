#ifndef WS_SERVER_H
#define WS_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Estructura para datos de telemetría
 */
typedef struct {
    char object_type[32];    // Tipo de objeto detectado
    float distance_cm;       // Distancia en centímetros
    float angle_deg;         // Ángulo relativo al centro
    int pixel_x;             // Coordenada X en píxeles
    int pixel_y;             // Coordenada Y en píxeles
    float world_x;           // Coordenada X en el mundo (cm)
    float world_y;           // Coordenada Y en el mundo (cm)
    uint32_t pixel_count;    // Cantidad de píxeles detectados
    bool detected;           // Si hay objeto detectado
    uint32_t timestamp_ms;   // Timestamp en milisegundos
} telemetry_data_t;

/**
 * @brief Inicializa el servidor WebSocket
 * 
 * Crea un servidor HTTP con soporte para WebSockets en el puerto 80.
 * Maneja tanto telemetría (JSON) como video streaming (JPEG binario).
 * 
 * @return ESP_OK si la inicialización fue exitosa
 */
esp_err_t ws_server_start(void);

/**
 * @brief Detiene el servidor WebSocket
 * 
 * @return ESP_OK si se detuvo correctamente
 */
esp_err_t ws_server_stop(void);

/**
 * @brief Envía datos de telemetría a todos los clientes conectados
 * 
 * Envía un mensaje JSON con los datos de detección.
 * Usa httpd_ws_send_frame_async para no bloquear.
 * 
 * @param telemetry Datos de telemetría a enviar
 * @return ESP_OK si se envió correctamente
 */
esp_err_t ws_server_send_telemetry(const telemetry_data_t *telemetry);

/**
 * @brief Envía un frame de video JPEG a todos los clientes
 * 
 * Envía la imagen como WebSocket binary frame.
 * Usa httpd_ws_send_frame_async para transmisión asíncrona.
 * 
 * @param jpeg_data Buffer con datos JPEG
 * @param jpeg_len Tamaño del buffer
 * @return ESP_OK si se envió correctamente
 */
esp_err_t ws_server_send_video_frame(const uint8_t *jpeg_data, size_t jpeg_len);

/**
 * @brief Obtiene el número de clientes WebSocket conectados
 * 
 * @return Número de clientes conectados
 */
uint8_t ws_server_get_clients_count(void);

/**
 * @brief Verifica si hay al menos un cliente conectado
 * 
 * @return true si hay clientes conectados
 */
bool ws_server_has_clients(void);

#endif // WS_SERVER_H
