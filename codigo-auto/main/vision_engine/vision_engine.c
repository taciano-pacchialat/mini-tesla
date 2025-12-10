/**
 * @file vision_engine.c
 * @brief Detección simple de objeto naranja para bloqueo de movimiento.
 */

#include "vision_engine.h"
#include "../hardware_config.h"
#include "ws_client/ws_client.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "driver/gpio.h"          // ← nuevo include
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "[Vision]";

// Estado global restaurado
static TaskHandle_t s_task_handle = NULL;
static SemaphoreHandle_t s_result_mutex = NULL;
static vision_result_t s_last_result = {0};
static bool s_task_running = false;
static bool s_reverse_only_mode = false;
static int64_t s_last_stream_us = 0; // seguimiento de streaming




/* Parámetros del detector */
// Ajuste para objeto naranja pálido/brillante en pantalla

#define ORANGE_R_MIN            140
#define ORANGE_G_MIN             120
#define ORANGE_G_MAX            150
#define ORANGE_B_MAX             130

#define ORANGE_RG_DELTA_MIN      30
#define ORANGE_RB_DELTA_MIN     50

#define ORANGE_MIN_PIXELS        300
#define ORANGE_MIN_FRACTION      0.0075f
#define ORANGE_MIN_PIXELS_PER_ROW 25
#define ORANGE_MIN_ROWS          10
#define ORANGE_DISTANCE_LOCK_CM  20.0f




#if (CAM_PIXEL_FORMAT != PIXFORMAT_RGB565)
#error "vision_engine requiere CAM_PIXEL_FORMAT = PIXFORMAT_RGB565"
#endif

static inline uint8_t rgb565_r(uint16_t pixel) { return (pixel & 0xF800) >> 8; }
static inline uint8_t rgb565_g(uint16_t pixel) { return (pixel & 0x07E0) >> 3; }
static inline uint8_t rgb565_b(uint16_t pixel) { return (pixel & 0x001F) << 3; }

static inline bool pixel_is_orange(uint16_t pixel)
{
    uint8_t r = rgb565_r(pixel);
    uint8_t g = rgb565_g(pixel);
    uint8_t b = rgb565_b(pixel);

    if (r < ORANGE_R_MIN || g < ORANGE_G_MIN || g > ORANGE_G_MAX || b > ORANGE_B_MAX) {
        return false;
    }

    int rg_delta = r - g;
    int rb_delta = r - b;

    return (rg_delta >= ORANGE_RG_DELTA_MIN) && (rb_delta >= ORANGE_RB_DELTA_MIN);
}

static inline float estimate_distance(int pixel_width)
{
    if (pixel_width <= 0) {
        return 999.0f;
    }
    return (KNOWN_OBJECT_WIDTH_CM * CAMERA_FOCAL_LENGTH_PX) / (float)pixel_width;
}

static esp_err_t configure_camera(void)
{
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_Y9,
        .pin_d6 = CAM_PIN_Y8,
        .pin_d5 = CAM_PIN_Y7,
        .pin_d4 = CAM_PIN_Y6,
        .pin_d3 = CAM_PIN_Y5,
        .pin_d2 = CAM_PIN_Y4,
        .pin_d1 = CAM_PIN_Y3,
        .pin_d0 = CAM_PIN_Y2,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = CAM_FREQ_HZ,
        .ledc_timer = LEDC_TIMER_1,
        .ledc_channel = LEDC_CHANNEL_2,
        .pixel_format = CAM_PIXEL_FORMAT,
        .frame_size = CAM_FRAME_SIZE,
        .jpeg_quality = CAM_JPEG_QUALITY,
        .fb_count = CAM_FB_COUNT,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed (0x%x)", err);
        return err;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_pixformat(sensor, PIXFORMAT_RGB565);
        sensor->set_framesize(sensor, CAM_FRAME_SIZE);
        if (sensor->set_whitebal)   sensor->set_whitebal(sensor, 1);  // AWB ON
        if (sensor->set_awb_gain)   sensor->set_awb_gain(sensor, 1);
        if (sensor->set_wb_mode)    sensor->set_wb_mode(sensor, 0);   // modo Auto neutro
        if (sensor->set_brightness) sensor->set_brightness(sensor, 0);
        if (sensor->set_contrast)   sensor->set_contrast(sensor, 0);
        if (sensor->set_saturation) sensor->set_saturation(sensor, 0);

    }


    ESP_LOGI(TAG, "Camera ready: %dx%d RGB565", IMAGE_WIDTH, IMAGE_HEIGHT);
    return ESP_OK;
}

static void stream_frame_over_ws(camera_fb_t *fb)
{
    if (!ws_client_is_connected() || !ws_client_stream_enabled()) {
        return;
    }

    int64_t now = esp_timer_get_time();
    if ((now - s_last_stream_us) < STREAM_MIN_INTERVAL_US) {
        return;
    }

    const uint8_t *payload = fb->buf;
    size_t payload_len = fb->len;
    uint8_t *jpeg_buf = NULL;
    bool owns_buffer = false;

    if (fb->format != PIXFORMAT_JPEG) {
        if (!frame2jpg(fb, STREAM_JPEG_QUALITY, &jpeg_buf, &payload_len)) {
            ESP_LOGW(TAG, "frame2jpg failed");
            return;
        }
        payload = jpeg_buf;
        owns_buffer = true;
    }

    if (payload_len > WS_MAX_PAYLOAD_SIZE) {
        ESP_LOGW(TAG, "JPEG %dB > límite %dB; reduce calidad o tamaño",
                 (int)payload_len, WS_MAX_PAYLOAD_SIZE);
    } else {
        if (ws_client_send_frame(payload, payload_len) == ESP_OK) {
            s_last_stream_us = now;
        }
    }

    if (owns_buffer && jpeg_buf) {
        free(jpeg_buf);
    }
}

static void analyze_frame(vision_result_t *result)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGW(TAG, "Frame grab failed");
        *result = (vision_result_t){0};
        return;
    }

    if (fb->format != PIXFORMAT_RGB565) {
        ESP_LOGW(TAG, "Unexpected pixel format: %d", fb->format);
        esp_camera_fb_return(fb);
        *result = (vision_result_t){0};
        return;
    }

    const uint16_t *pixels = (const uint16_t *)fb->buf;
    uint32_t min_pixels = (uint32_t)(fb->width * fb->height * ORANGE_MIN_FRACTION);
    if (min_pixels < ORANGE_MIN_PIXELS) {
        min_pixels = ORANGE_MIN_PIXELS;
    }

    int min_x = fb->width, max_x = -1;
    int min_y = fb->height, max_y = -1;
    uint32_t hits = 0;
    uint32_t valid_rows = 0;

    for (int y = 0; y < fb->height; ++y) {
        uint32_t row_hits = 0;
        const uint16_t *row = pixels + y * fb->width;
        for (int x = 0; x < fb->width; ++x) {
            if (!pixel_is_orange(row[x])) {
                continue;
            }
            row_hits++;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
        }

        if (row_hits >= ORANGE_MIN_PIXELS_PER_ROW) {
            valid_rows++;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
            hits += row_hits;
        }
    }

    vision_result_t local = {
        .obstacle_detected = (hits >= min_pixels) && (valid_rows >= ORANGE_MIN_ROWS),
        .distance_cm = 999.0f,
        .reverse_only = false
    };

    if (local.obstacle_detected && max_x >= min_x && max_y >= min_y) {
        int bbox_width  = (max_x - min_x) + 1;
        int bbox_height = (max_y - min_y) + 1;
        int dominant_px = bbox_width > bbox_height ? bbox_width : bbox_height;
        local.distance_cm = estimate_distance(dominant_px);
        local.reverse_only = (local.distance_cm <= ORANGE_DISTANCE_LOCK_CM);
    }

    stream_frame_over_ws(fb);
    esp_camera_fb_return(fb);
    *result = local;
}

static void vision_task(void *param)
{
    ESP_LOGI(TAG, "Vision task running on core %d", xPortGetCoreID());
    const TickType_t period = pdMS_TO_TICKS(120); // ~8 FPS
    TickType_t last = xTaskGetTickCount();

    while (s_task_running) {
        vision_result_t result = {0};
        analyze_frame(&result);

        if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(5))) {
            s_last_result = result;
            s_reverse_only_mode = result.reverse_only;
            xSemaphoreGive(s_result_mutex);
        }

        vTaskDelayUntil(&last, period);
    }

    ESP_LOGI(TAG, "Vision task stopped");
    vTaskDelete(NULL);
}

esp_err_t vision_engine_init(void)
{
    if (s_result_mutex == NULL) {
        s_result_mutex = xSemaphoreCreateMutex();
        if (s_result_mutex == NULL) {
            ESP_LOGE(TAG, "Mutex alloc failed");
            return ESP_FAIL;
        }
    }
    return configure_camera();
}

esp_err_t vision_engine_start(void)
{
    if (s_task_running) {
        ESP_LOGW(TAG, "Vision already running");
        return ESP_OK;
    }

    s_task_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        vision_task,
        "vision_task",
        4096,
        NULL,
        5,
        &s_task_handle,
        1);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task creation failed");
        s_task_running = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t vision_engine_stop(void)
{
    if (!s_task_running) {
        return ESP_OK;
    }

    s_task_running = false;
    vTaskDelay(pdMS_TO_TICKS(100));
    s_task_handle = NULL;
    return ESP_OK;
}

esp_err_t vision_engine_get_result(vision_result_t *result)
{
    if (!result) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(20))) {
        memcpy(result, &s_last_result, sizeof(vision_result_t));
        xSemaphoreGive(s_result_mutex);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

bool vision_engine_is_veto_active(void)
{
    bool active;
    if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(10))) {
        active = s_reverse_only_mode;
        xSemaphoreGive(s_result_mutex);
    } else {
        active = false;
    }
    return active;
}

bool vision_engine_command_allowed(control_command_t command)
{
    if (!vision_engine_is_veto_active()) {
        return true;
    }

    switch (command) {
    case CONTROL_CMD_BACKWARD:
    case CONTROL_CMD_STOP:
        return true;
    default:
        return true; // poner en false para bloquear otros comandos
    }
}
