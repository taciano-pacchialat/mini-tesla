#ifndef WS_SERVER_H
#define WS_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FRAME_SOURCE_ESP32S3 = 0,
    FRAME_SOURCE_ESP32CAM = 1,
} frame_source_t;

/**
 * @brief Inicializa el servidor WebSocket
 * 
 * Crea un servidor HTTP con soporte para WebSockets en el puerto 80.
 * Maneja video streaming (JPEG binario) y control manual vía WebSocket.
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
 * @brief Envía un frame de video JPEG a todos los clientes
 * 
 * Envía la imagen como WebSocket binary frame.
 * Usa httpd_ws_send_frame_async para transmisión asíncrona.
 * 
 * @param jpeg_data Buffer con datos JPEG
 * @param jpeg_len Tamaño del buffer
 * @return ESP_OK si se envió correctamente
 */
esp_err_t ws_server_send_video_frame(frame_source_t source,
                                     const uint8_t *jpeg_data,
                                     size_t jpeg_len);

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
