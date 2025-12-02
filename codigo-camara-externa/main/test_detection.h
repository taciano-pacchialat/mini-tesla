#ifndef TEST_DETECTION_H
#define TEST_DETECTION_H

/**
 * @brief Ejecuta un test único de detección con todos los colores predefinidos
 */
void test_object_detection(void);

/**
 * @brief Tarea continua de monitoreo de detección
 * @param pvParameters Puntero a color_range_t para el color a detectar
 */
void detection_monitor_task(void *pvParameters);

#endif // TEST_DETECTION_H
