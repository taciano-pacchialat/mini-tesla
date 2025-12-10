#include "vision.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "Vision";

#define DETECTION_MIN_PIXELS 20U

// Rangos de color predefinidos en HSV (0-255)
const color_range_t COLOR_RED = {
    .h_min = 226, .h_max = 20, // Wrap: [226-255] U [0-20]
    .s_min = 100,
    .s_max = 255,
    .v_min = 120,
    .v_max = 255};

const color_range_t COLOR_GREEN = {
    .h_min = 57,
    .h_max = 113,
    .s_min = 60,
    .s_max = 255,
    .v_min = 60,
    .v_max = 255};

const color_range_t COLOR_BLUE = {
    .h_min = 198,
    .h_max = 255,
    .s_min = 80,
    .s_max = 255,
    .v_min = 80,
    .v_max = 255};

const color_range_t COLOR_YELLOW = {
    .h_min = 50,
    .h_max = 78,
    .s_min = 100,
    .s_max = 255,
    .v_min = 120,
    .v_max = 255};

// Obstáculo
const color_range_t COLOR_ORANGE = {
    .h_min = 14,
    .h_max = 42,
    .s_min = 120,
    .s_max = 255,
    .v_min = 180,
    .v_max = 255};

void rgb565_to_hsv_fast(uint16_t pixel, uint8_t *h, uint8_t *s, uint8_t *v)
{
    uint8_t r = (pixel & 0xF800) >> 8;
    uint8_t g = (pixel & 0x07E0) >> 3;
    uint8_t b = (pixel & 0x001F) << 3;

    uint8_t min_val = (r < g) ? (r < b ? r : b) : (g < b ? g : b);
    uint8_t max_val = (r > g) ? (r > b ? r : b) : (g > b ? g : b);
    uint8_t delta = max_val - min_val;

    *v = max_val;

    if (delta == 0)
    {
        *h = 0;
        *s = 0;
        return;
    }

    *s = (uint16_t)(delta << 8) / (max_val ? max_val : 1);

    if (r == max_val)
    {
        if (g >= b)
            *h = (uint8_t)((43 * (g - b)) / delta);
        else
            *h = (uint8_t)(255 + (43 * (g - b)) / delta);
    }
    else if (g == max_val)
    {
        *h = (uint8_t)(85 + (43 * (b - r)) / delta);
    }
    else
    {
        *h = (uint8_t)(171 + (43 * (r - g)) / delta);
    }
}

static inline bool pixel_in_range(uint8_t h, uint8_t s, uint8_t v, const color_range_t *range)
{
    if (s < range->s_min || s > range->s_max)
        return false;
    if (v < range->v_min || v > range->v_max)
        return false;

    if (range->h_min <= range->h_max)
    {
        return (h >= range->h_min && h <= range->h_max);
    }
    else
    {
        return (h >= range->h_min || h <= range->h_max);
    }
}

/**
 * Detecta objeto por color y calcula centroide
 */
void detect_object_by_color(const uint16_t *frame_buffer,
                            int width,
                            int height,
                            const color_range_t *color_range,
                            const homography_matrix_t *h_matrix,
                            detection_result_t *result)
{
    if (!frame_buffer || !color_range || !result)
    {
        ESP_LOGE(TAG, "Invalid parameters");
        return;
    }

    // Inicializar resultado
    result->centroid_x = -1;
    result->centroid_y = -1;
    result->pixel_count = 0;
    result->detected = false;
    result->world_coords.x = 0.0f;
    result->world_coords.y = 0.0f;

    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    uint32_t count = 0;

    // Procesar cada píxel del frame
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = y * width + x;
            uint16_t pixel = frame_buffer[idx];

            uint8_t h, s, v;
            rgb565_to_hsv_fast(pixel, &h, &s, &v);

            if (pixel_in_range(h, s, v, color_range))
            {
                sum_x += x;
                sum_y += y;
                count++;
            }
        }
    }

    // Calcular centroide si se detectaron píxeles
    if (count >= DETECTION_MIN_PIXELS)
    {
        result->centroid_x = sum_x / count;
        result->centroid_y = sum_y / count;
        result->pixel_count = count;
        result->detected = true;

        // Transformar coordenadas de píxeles a mundo real si hay matriz
        if (h_matrix)
        {
            pixel_point_t pixel_pt = {.u = result->centroid_x, .v = result->centroid_y};
            homography_transform(h_matrix, pixel_pt, &result->world_coords);

            const float kMin = 0.0f;
            result->world_coords.x = fmaxf(result->world_coords.x, kMin);
            result->world_coords.y = fmaxf(result->world_coords.y, kMin);
        }
        else
        {
            ESP_LOGI(TAG, "Object detected at (%d, %d) with %lu pixels",
                     result->centroid_x, result->centroid_y, result->pixel_count);
        }
    }
    else
    {
        ESP_LOGD(TAG, "No object detected");
    }
}
