#ifndef TEST_HOMOGRAPHY_H
#define TEST_HOMOGRAPHY_H

/**
 * @brief Ejecuta todos los tests del sistema de homografía y distancia
 */
void run_homography_tests(void);

/**
 * @brief Test básico de transformación homográfica
 */
void test_homography_basic(void);

/**
 * @brief Test de detección con cálculo de distancia
 */
void test_detection_with_distance(void);

/**
 * @brief Test de detección multi-color con distancia
 */
void test_multicolor_detection_with_distance(void);

#endif // TEST_HOMOGRAPHY_H
