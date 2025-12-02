#ifndef VISION_TASK_H
#define VISION_TASK_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/**
 * @brief Configuración de la tarea de visión
 */
#define VISION_TASK_STACK_SIZE (8192)
#define VISION_TASK_PRIORITY (5)
#define VISION_TASK_CORE_ID (1)  // Core 1 (Application CPU)

/**
 * @brief Cola para frames procesados
 */
#define VISION_QUEUE_SIZE (2)

/**
 * @brief Estructura de frame procesado para streaming
 */
typedef struct {
    uint8_t *jpeg_data;
    size_t jpeg_len;
    uint32_t timestamp_ms;
} processed_frame_t;

/**
 * @brief Inicia la tarea de visión en Core 1
 * 
 * Esta tarea ejecuta el bucle de captura de cámara, procesamiento
 * de detección y generación de frames JPEG para streaming.
 * 
 * @return ESP_OK si se inició correctamente
 */
esp_err_t vision_task_start(void);

/**
 * @brief Detiene la tarea de visión
 * 
 * @return ESP_OK si se detuvo correctamente
 */
esp_err_t vision_task_stop(void);

/**
 * @brief Obtiene la cola de frames procesados
 * 
 * @return Handle de la cola
 */
QueueHandle_t vision_task_get_queue(void);

/**
 * @brief Configura el color objetivo para detección
 * 
 * @param color_name "RED", "GREEN", "BLUE", "YELLOW"
 * @return ESP_OK si se configuró correctamente
 */
esp_err_t vision_task_set_target_color(const char *color_name);

/**
 * @brief Habilita/deshabilita el procesamiento de visión
 * 
 * @param enable true para habilitar, false para deshabilitar
 */
void vision_task_enable_processing(bool enable);

#endif // VISION_TASK_H
