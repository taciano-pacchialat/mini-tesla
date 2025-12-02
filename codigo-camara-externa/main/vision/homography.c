#include "homography.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"

static const char *TAG = "Homography";

void homography_init(homography_matrix_t *H, const float h_coeffs[9])
{
    if (H == NULL || h_coeffs == NULL)
    {
        return;
    }
    memcpy(H->h, h_coeffs, 9 * sizeof(float));
}

void homography_transform(const homography_matrix_t *H,
                          pixel_point_t pixel,
                          world_point_t *world)
{
    if (H == NULL || world == NULL)
    {
        return;
    }

    // Multiplicación matriz-vector: [x', y', w'] = H * [u, v, 1]
    float u = (float)pixel.u;
    float v = (float)pixel.v;

    float x_h = H->h[0] * u + H->h[1] * v + H->h[2];
    float y_h = H->h[3] * u + H->h[4] * v + H->h[5];
    float w_h = H->h[6] * u + H->h[7] * v + H->h[8];

    // Normalización por coordenada homogénea
    if (fabs(w_h) > 1e-6)
    {
        world->x = x_h / w_h;
        world->y = y_h / w_h;
    }
    else
    {
        world->x = 0.0f;
        world->y = 0.0f;
        ESP_LOGW(TAG, "Division by zero in homography transformation");
    }
}

void homography_load_default(homography_matrix_t *H,
                             int image_width, int image_height,
                             float real_width, float real_height)
{
    if (H == NULL)
    {
        return;
    }

    // Matriz de homografía simple: escala + traslación
    // Asume que la cámara está perfectamente cenital sin distorsión
    float scale_x = real_width / (float)image_width;
    float scale_y = real_height / (float)image_height;

    // Matriz identidad escalada y centrada
    float h_default[9] = {
        scale_x, 0.0f, -real_width / 2.0f,
        0.0f, scale_y, -real_height / 2.0f,
        0.0f, 0.0f, 1.0f};

    homography_init(H, h_default);
    ESP_LOGI(TAG, "Loaded default homography: scale_x=%.2f, scale_y=%.2f", scale_x, scale_y);
}

// Implementación simplificada de cálculo de homografía usando DLT
// Para una implementación completa, se recomienda pre-calcular en PC
bool homography_calculate(homography_matrix_t *H,
                          const pixel_point_t src_points[4],
                          const world_point_t dst_points[4])
{
    if (H == NULL || src_points == NULL || dst_points == NULL)
    {
        return false;
    }

    // Esta es una implementación simplificada
    // Para producción, calcular H offline usando cv::findHomography
    // y cargar los coeficientes desde flash/SPIFFS

    ESP_LOGW(TAG, "Full homography calculation not implemented on-chip");
    ESP_LOGW(TAG, "Use homography_init() with pre-calculated coefficients");

    // Por ahora, usar transformación por defecto
    homography_load_default(H, 640, 480, 100.0f, 100.0f);

    return false;
}
