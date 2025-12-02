#ifndef HOMOGRAPHY_H
#define HOMOGRAPHY_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Estructura para almacenar la matriz de homografía 3x3
 */
typedef struct {
    float h[9];  // Matriz almacenada en orden row-major
} homography_matrix_t;

/**
 * @brief Punto en coordenadas de píxel (imagen)
 */
typedef struct {
    int u;  // Coordenada x en píxeles
    int v;  // Coordenada y en píxeles
} pixel_point_t;

/**
 * @brief Punto en coordenadas del mundo (real)
 */
typedef struct {
    float x;  // Coordenada x en cm
    float y;  // Coordenada y en cm
} world_point_t;

/**
 * @brief Inicializa la matriz de homografía con valores pre-calculados
 * 
 * @param H Puntero a la matriz de homografía
 * @param h_coeffs Array de 9 coeficientes [h00, h01, h02, h10, h11, h12, h20, h21, h22]
 */
void homography_init(homography_matrix_t *H, const float h_coeffs[9]);

/**
 * @brief Calcula la matriz de homografía a partir de 4 puntos de correspondencia
 * 
 * @param H Puntero a la matriz de homografía a calcular
 * @param src_points Array de 4 puntos en la imagen (píxeles)
 * @param dst_points Array de 4 puntos en el mundo (cm)
 * @return true si el cálculo fue exitoso, false en caso contrario
 */
bool homography_calculate(homography_matrix_t *H, 
                         const pixel_point_t src_points[4],
                         const world_point_t dst_points[4]);

/**
 * @brief Transforma un punto de coordenadas de píxel a coordenadas del mundo
 * 
 * @param H Matriz de homografía
 * @param pixel Punto en píxeles
 * @param world Punto de salida en coordenadas reales (cm)
 */
void homography_transform(const homography_matrix_t *H, 
                         pixel_point_t pixel,
                         world_point_t *world);

/**
 * @brief Carga una matriz de homografía por defecto para calibración rápida
 * 
 * @param H Puntero a la matriz de homografía
 * @param image_width Ancho de la imagen en píxeles
 * @param image_height Alto de la imagen en píxeles
 * @param real_width Ancho real del área visible en cm
 * @param real_height Alto real del área visible en cm
 */
void homography_load_default(homography_matrix_t *H,
                             int image_width, int image_height,
                             float real_width, float real_height);

#endif // HOMOGRAPHY_H
