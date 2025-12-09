#ifndef VISION_H
#define VISION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "homography.h"

/**
 * @brief Rango de color en HSV (0-255)
 */
typedef struct
{
    uint8_t h_min;
    uint8_t h_max;
    uint8_t s_min;
    uint8_t s_max;
    uint8_t v_min;
    uint8_t v_max;
} color_range_t;

/**
 * @brief Estructura para almacenar el resultado de detección
 */
typedef struct
{
    int centroid_x;             // Coordenada X del centroide en píxeles (-1 si no detectado)
    int centroid_y;             // Coordenada Y del centroide en píxeles (-1 si no detectado)
    world_point_t world_coords; // Coordenadas en el mundo real (cm)
    uint32_t pixel_count;       // Cantidad de píxeles detectados
    bool detected;              // True si se detectó el objeto
} detection_result_t;

void rgb565_to_hsv_fast(uint16_t pixel, uint8_t *h, uint8_t *s, uint8_t *v);

/**
 * @brief Detecta un objeto de color en un frame RGB565 usando umbrales HSV
 *
 * @param frame_buffer Buffer de la imagen en formato RGB565
 * @param width Ancho de la imagen en píxeles
 * @param height Alto de la imagen en píxeles
 * @param color_range Rango de color en HSV para detectar
 * @param h_matrix Matriz de homografía para calcular coordenadas del mundo real (puede ser NULL)
 * @param result Estructura para almacenar el resultado
 */
void detect_object_by_color(const uint16_t *frame_buffer,
                            int width,
                            int height,
                            const color_range_t *color_range,
                            const homography_matrix_t *h_matrix,
                            detection_result_t *result);

/**
 * @brief Rangos de color predefinidos para detección común
 */
extern const color_range_t COLOR_RED;
extern const color_range_t COLOR_GREEN;
extern const color_range_t COLOR_BLUE;
extern const color_range_t COLOR_YELLOW;
extern const color_range_t COLOR_ORANGE;

#endif // VISION_H
