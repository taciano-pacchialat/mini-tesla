#include "vision.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "Vision";

// Rangos de color predefinidos (HSV)
const color_range_t COLOR_RED = {
    .h_min = 0, .h_max = 20, // Rojo está en ambos extremos del círculo
    .s_min = 100,
    .s_max = 255,
    .v_min = 100,
    .v_max = 255};

const color_range_t COLOR_GREEN = {
    .h_min = 60, .h_max = 100, .s_min = 80, .s_max = 255, .v_min = 80, .v_max = 255};

const color_range_t COLOR_BLUE = {
    .h_min = 140, .h_max = 180, .s_min = 80, .s_max = 255, .v_min = 80, .v_max = 255};

const color_range_t COLOR_YELLOW = {
    .h_min = 35, .h_max = 55, .s_min = 100, .s_max = 255, .v_min = 100, .v_max = 255};

/**
 * Conversión RGB565 a HSV optimizada con aritmética de enteros
 * Basada en la investigación de optimización para ESP32
 */
void rgb565_to_hsv_fast(uint16_t pixel, uint8_t *h, uint8_t *s, uint8_t *v)
{
    // Extracción eficiente de componentes usando máscaras de bits
    // RGB565: RRRRR GGGGGG BBBBB
    uint8_t r = (pixel & 0xF800) >> 8; // 5 bits expandidos a 8 bits
    uint8_t g = (pixel & 0x07E0) >> 3; // 6 bits expandidos a 8 bits
    uint8_t b = (pixel & 0x001F) << 3; // 5 bits expandidos a 8 bits

    // Encontrar mín y máx usando comparaciones en cascada
    uint8_t min_val = (r < g) ? (r < b ? r : b) : (g < b ? g : b);
    uint8_t max_val = (r > g) ? (r > b ? r : b) : (g > b ? g : b);
    uint8_t delta = max_val - min_val;

    *v = max_val; // Valor es el máximo

    if (delta == 0)
    {
        *h = 0;
        *s = 0;
        return;
    }

    // Cálculo de Saturación sin flotantes: (delta * 255) / max_val
    // Optimización: desplazamiento izquierdo en lugar de multiplicación
    *s = (uint16_t)(delta << 8) / max_val;

    // Cálculo de Matiz (Hue) usando aritmética entera
    // El factor 43 aproxima (255 / 6) para escalar los sectores de 60 grados
    if (r == max_val)
    {
        // Sector rojo-amarillo
        if (g >= b)
            *h = (43 * (g - b)) / delta;
        else
            *h = 255 + (43 * (g - b)) / delta; // Wraparound
    }
    else if (g == max_val)
    {
        // Sector verde-cian
        *h = 85 + (43 * (b - r)) / delta;
    }
    else
    {
        // Sector azul-magenta
        *h = 171 + (43 * (r - g)) / delta;
    }
}

/**
 * Verifica si un píxel HSV está dentro del rango de color especificado
 */
static inline bool pixel_in_range(uint8_t h, uint8_t s, uint8_t v, const color_range_t *range)
{
    // Verificar saturación y valor primero (más rápido)
    if (s < range->s_min || s > range->s_max)
        return false;
    if (v < range->v_min || v > range->v_max)
        return false;

    // Verificar matiz (puede tener wraparound para rojo)
    if (range->h_min <= range->h_max)
    {
        // Rango normal
        return (h >= range->h_min && h <= range->h_max);
    }
    else
    {
        // Wraparound (ej: rojo que cruza 0)
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

            // Convertir a HSV
            uint8_t h, s, v;
            rgb565_to_hsv_fast(pixel, &h, &s, &v);

            // Verificar si está en rango
            if (pixel_in_range(h, s, v, color_range))
            {
                sum_x += x;
                sum_y += y;
                count++;
            }
        }
    }

    // Calcular centroide si se detectaron píxeles
    if (count > 0)
    {
        result->centroid_x = sum_x / count;
        result->centroid_y = sum_y / count;
        result->pixel_count = count;
        result->detected = true;

        // Transformar coordenadas de píxeles a mundo real si hay matriz
        if (h_matrix)
        {
            pixel_point_t pixel_pt = {
                .u = result->centroid_x,
                .v = result->centroid_y};
            homography_transform(h_matrix, pixel_pt, &result->world_coords);

            ESP_LOGI(TAG, "Object detected at pixel (%d, %d) -> world (%.2f cm, %.2f cm), %lu pixels",
                     result->centroid_x, result->centroid_y,
                     result->world_coords.x, result->world_coords.y,
                     result->pixel_count);
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
