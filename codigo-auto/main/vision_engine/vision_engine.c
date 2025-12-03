/**
 * @file vision_engine.c
 * @brief Local vision processing implementation for ESP32-CAM
 *
 * CRITICAL NOTES:
 * - Uses C-style OpenCV macros (CV_*) not C++ constants (cv::COLOR_*)
 * - RGB565 requires endianness correction for OV2640 sensor
 * - Zero-copy architecture to minimize memory operations
 * - All frame buffers allocated in external PSRAM
 */

#include "vision_engine.h"
#include "../hardware_config.h"
#include "../ws_client/ws_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

// OpenCV includes - using C-style port for ESP32
// NOTE: esp32-camera component must be added to dependencies
#include "esp_camera.h"

// TODO: Verify exact OpenCV include path for esp32-opencv component
// #include "opencv2/core.h"
// #include "opencv2/imgproc.h"

static const char *TAG = "[Vision]";

#define STREAM_FRAME_INTERVAL 3
#define STREAM_JPEG_QUALITY_DEFAULT 60
#define STREAM_JPEG_QUALITY_MIN 30
#define STREAM_JPEG_QUALITY_STEP 10

typedef struct
{
    uint8_t h_min;
    uint8_t h_max;
    uint8_t s_min;
    uint8_t s_max;
    uint8_t v_min;
    uint8_t v_max;
    bool wrap;
} hsv_range_t;

static const hsv_range_t kGreenRange = {
    .h_min = HSV_GREEN_H_MIN,
    .h_max = HSV_GREEN_H_MAX,
    .s_min = HSV_GREEN_S_MIN,
    .s_max = HSV_GREEN_S_MAX,
    .v_min = HSV_GREEN_V_MIN,
    .v_max = HSV_GREEN_V_MAX,
    .wrap = (HSV_GREEN_H_MIN > HSV_GREEN_H_MAX)};

static inline void rgb565_to_hsv_fast(uint16_t pixel, uint8_t *h, uint8_t *s, uint8_t *v)
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

    *s = (uint16_t)(delta << 8) / max_val;

    if (r == max_val)
    {
        *h = (g >= b) ? (43 * (g - b)) / delta : 255 + (43 * (g - b)) / delta;
    }
    else if (g == max_val)
    {
        *h = 85 + (43 * (b - r)) / delta;
    }
    else
    {
        *h = 171 + (43 * (r - g)) / delta;
    }
}

static inline bool hsv_in_range(uint8_t h, uint8_t s, uint8_t v, const hsv_range_t *range)
{
    if (s < range->s_min || s > range->s_max)
    {
        return false;
    }

    if (v < range->v_min || v > range->v_max)
    {
        return false;
    }

    if (!range->wrap)
    {
        return (h >= range->h_min) && (h <= range->h_max);
    }

    return (h >= range->h_min) || (h <= range->h_max);
}

// ============================================================================
// GLOBAL STATE
// ============================================================================

static TaskHandle_t s_vision_task_handle = NULL;
static SemaphoreHandle_t s_result_mutex = NULL;
static vision_result_t s_last_result = {0};
static bool s_veto_active = false;
static bool s_debug_enabled = false;
static bool s_task_running = false;

// Statistics
static uint32_t s_frame_counter = 0;
static uint64_t s_total_process_time_us = 0;

static bool stream_frame_over_ws(camera_fb_t *fb)
{
    if (!ws_client_is_connected())
    {
        return false;
    }

    int quality = STREAM_JPEG_QUALITY_DEFAULT;

    while (quality >= STREAM_JPEG_QUALITY_MIN)
    {
        uint8_t *jpeg_buf = NULL;
        size_t jpeg_len = 0;

        if (!frame2jpg(fb, quality, &jpeg_buf, &jpeg_len))
        {
            ESP_LOGE(TAG, "frame2jpg failed at quality %d", quality);
            return false;
        }

        if (jpeg_len > (WS_MAX_PAYLOAD_SIZE - 128) && quality > STREAM_JPEG_QUALITY_MIN)
        {
            ESP_LOGW(TAG, "JPEG %d bytes > limit %d @Q%d, retrying",
                     (int)jpeg_len, WS_MAX_PAYLOAD_SIZE, quality);
            free(jpeg_buf);
            quality -= STREAM_JPEG_QUALITY_STEP;
            continue;
        }

        esp_err_t err = ws_client_send_frame(jpeg_buf, jpeg_len);
        free(jpeg_buf);

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "WebSocket send failed: %s", esp_err_to_name(err));
            return false;
        }

        return true;
    }

    ESP_LOGE(TAG, "Unable to compress frame under %d bytes", WS_MAX_PAYLOAD_SIZE);
    return false;
}

// ============================================================================
// CAMERA INITIALIZATION
// ============================================================================

/**
 * @brief Configure camera with AI Thinker ESP32-CAM pinout
 */
static esp_err_t init_camera(void)
{
    ESP_LOGI(TAG, "Initializing OV2640 camera...");

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

        // Camera settings
        .xclk_freq_hz = 20000000, // 20MHz XCLK
        .ledc_timer = LEDC_TIMER_1,
        .ledc_channel = LEDC_CHANNEL_2,

        .pixel_format = CAM_PIXEL_FORMAT, // RGB565 for processing
        .frame_size = CAM_FRAME_SIZE,     // QVGA (320x240)
        .jpeg_quality = CAM_JPEG_QUALITY, // Not used for RGB565
        .fb_count = CAM_FB_COUNT,         // Double buffering
        .fb_location = CAM_FB_LOCATION,   // PSRAM (critical!)
        .grab_mode = CAMERA_GRAB_LATEST   // Always get latest frame
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return err;
    }

    // Camera sensor tuning
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL)
    {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }

    // Optimize for vision processing (not aesthetics)
    s->set_brightness(s, 0);                 // Default brightness
    s->set_contrast(s, 0);                   // Default contrast
    s->set_saturation(s, 0);                 // Default saturation
    s->set_special_effect(s, 0);             // No special effects
    s->set_whitebal(s, 1);                   // Auto white balance ON
    s->set_awb_gain(s, 1);                   // Auto white balance gain ON
    s->set_wb_mode(s, 0);                    // White balance mode auto
    s->set_exposure_ctrl(s, 1);              // Auto exposure ON
    s->set_aec2(s, 0);                       // AEC sensor ON
    s->set_ae_level(s, 0);                   // Auto exposure level 0
    s->set_aec_value(s, 300);                // Auto exposure value
    s->set_gain_ctrl(s, 1);                  // Auto gain ON
    s->set_agc_gain(s, 0);                   // Auto gain value
    s->set_gainceiling(s, (gainceiling_t)0); // Gain ceiling
    s->set_bpc(s, 0);                        // Black pixel correction OFF
    s->set_wpc(s, 1);                        // White pixel correction ON
    s->set_raw_gma(s, 1);                    // Gamma correction ON
    s->set_lenc(s, 1);                       // Lens correction ON
    s->set_hmirror(s, 0);                    // Horizontal mirror OFF
    s->set_vflip(s, 0);                      // Vertical flip OFF
    s->set_dcw(s, 1);                        // Downsize enable ON
    s->set_colorbar(s, 0);                   // Color bar test pattern OFF

    ESP_LOGI(TAG, "Camera initialized successfully");
    ESP_LOGI(TAG, "Resolution: %dx%d, Format: RGB565, Buffers: %d (PSRAM)",
             IMAGE_WIDTH, IMAGE_HEIGHT, CAM_FB_COUNT);

    return ESP_OK;
}

// ============================================================================
// IMAGE PROCESSING FUNCTIONS
// ============================================================================

/**
 * @brief Estimate distance using pinhole camera model
 *
 * Formula: distance = (real_width * focal_length) / pixel_width
 *
 * @param pixel_width Width of object in pixels
 * @return Estimated distance in centimeters
 */
static inline float estimate_distance(int pixel_width)
{
    if (pixel_width <= 0)
        return 999.9f; // Invalid

    float distance = (KNOWN_OBJECT_WIDTH_CM * CAMERA_FOCAL_LENGTH_PX) / (float)pixel_width;
    return distance;
}

/**
 * @brief Process single frame for green obstacle detection
 *
 * Pipeline:
 * 1. Get RGB565 frame from camera (zero-copy)
 * 2. Convert RGB565 -> BGR888 -> HSV
 * 3. Threshold for green color
 * 4. Morphological filtering (noise removal)
 * 5. Find contours
 * 6. Calculate largest object bounding box
 * 7. Estimate distance
 *
 * @param[out] result Detection result structure
 * @return ESP_OK on success
 */
static esp_err_t process_frame(vision_result_t *result)
{
    uint64_t start_time = esp_timer_get_time();

    // 1. Capture frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGW(TAG, "Camera capture failed");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Frame captured: %dx%d, %zu bytes, format=%d",
             fb->width, fb->height, fb->len, fb->format);

    // Initialize result
    result->obstacle_detected = false;
    result->distance_cm = 999.9f;
    result->centroid_x = 0;
    result->centroid_y = 0;
    result->contour_area = 0;

    const uint16_t *pixels = (const uint16_t *)fb->buf;
    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    uint32_t hit_count = 0;
    int min_x = fb->width;
    int max_x = -1;
    int min_y = fb->height;
    int max_y = -1;

    for (int y = 0; y < fb->height; y++)
    {
        const uint16_t *row = pixels + (y * fb->width);
        for (int x = 0; x < fb->width; x++)
        {
            uint8_t h, s, v;
            rgb565_to_hsv_fast(row[x], &h, &s, &v);

            if (!hsv_in_range(h, s, v, &kGreenRange))
            {
                continue;
            }

            sum_x += x;
            sum_y += y;
            hit_count++;

            if (x < min_x)
                min_x = x;
            if (x > max_x)
                max_x = x;
            if (y < min_y)
                min_y = y;
            if (y > max_y)
                max_y = y;
        }
    }

    const int image_area = fb->width * fb->height;
    const int max_allowed_area = (int)(image_area * MAX_CONTOUR_AREA_RATIO);

    if (hit_count >= MIN_CONTOUR_AREA && hit_count < (uint32_t)max_allowed_area && max_x >= 0)
    {
        result->obstacle_detected = true;
        result->centroid_x = sum_x / hit_count;
        result->centroid_y = sum_y / hit_count;
        result->contour_area = hit_count;

        int bbox_width = (max_x - min_x) + 1;
        result->distance_cm = estimate_distance(bbox_width);

        ESP_LOGI(TAG, "Obstáculo verde: %.1f cm @ (%d,%d) area=%d",
                 result->distance_cm, result->centroid_x, result->centroid_y, result->contour_area);
    }
    else
    {
        result->obstacle_detected = false;
        result->distance_cm = 999.9f;
    }

    uint32_t frame_index = ++s_frame_counter;
    result->frame_count = frame_index;

    if ((frame_index % STREAM_FRAME_INTERVAL) == 0)
    {
        stream_frame_over_ws(fb);
    }

    // Return frame buffer to driver
    esp_camera_fb_return(fb);

    // Calculate processing time
    uint64_t end_time = esp_timer_get_time();
    result->processing_time_ms = (end_time - start_time) / 1000;
    if (result->frame_count == 0)
    {
        result->frame_count = ++s_frame_counter;
    }

    ESP_LOGD(TAG, "Frame processed in %u ms", result->processing_time_ms);

    return ESP_OK;
}

// ============================================================================
// VISION PROCESSING TASK
// ============================================================================

/**
 * @brief Main vision processing task (runs on Core 1)
 */
static void vision_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Vision task started on core %d", xPortGetCoreID());

    vision_result_t result;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frame_period = pdMS_TO_TICKS(100); // ~10 FPS target

    while (s_task_running)
    {
        // Process frame
        if (process_frame(&result) == ESP_OK)
        {
            // Update shared state (thread-safe)
            if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(10)))
            {
                memcpy(&s_last_result, &result, sizeof(vision_result_t));

                // Update veto flag
                s_veto_active = (result.obstacle_detected &&
                                 result.distance_cm < VETO_DISTANCE_THRESHOLD_CM);

                if (s_veto_active)
                {
                    ESP_LOGW(TAG, "VETO ACTIVE: Obstacle at %.1f cm (threshold %.1f cm)",
                             result.distance_cm, VETO_DISTANCE_THRESHOLD_CM);
                }

                xSemaphoreGive(s_result_mutex);
            }

            // Update statistics
            s_total_process_time_us += result.processing_time_ms * 1000;
        }

        // Rate limiting
        vTaskDelayUntil(&last_wake_time, frame_period);
    }

    ESP_LOGI(TAG, "Vision task stopped");
    vTaskDelete(NULL);
}

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

esp_err_t vision_engine_init(void)
{
    ESP_LOGI(TAG, "Initializing vision engine...");

    // Create mutex for thread-safe access
    s_result_mutex = xSemaphoreCreateMutex();
    if (s_result_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create result mutex");
        return ESP_FAIL;
    }

    // Initialize camera
    esp_err_t ret = init_camera();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ESP_LOGI(TAG, "Vision engine initialized successfully");
    return ESP_OK;
}

esp_err_t vision_engine_start(void)
{
    if (s_task_running)
    {
        ESP_LOGW(TAG, "Vision task already running");
        return ESP_OK;
    }

    s_task_running = true;

    // Create vision processing task on Core 1 (Application CPU)
    BaseType_t ret = xTaskCreatePinnedToCore(
        vision_task,
        "vision_task",
        8192, // Stack size (8KB - OpenCV needs substantial stack)
        NULL,
        6, // High priority (same as control task)
        &s_vision_task_handle,
        1 // Core 1 (Application CPU)
    );

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create vision task");
        s_task_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Vision processing task started on Core 1");
    return ESP_OK;
}

esp_err_t vision_engine_stop(void)
{
    if (!s_task_running)
    {
        return ESP_OK;
    }

    s_task_running = false;

    // Wait for task to terminate
    if (s_vision_task_handle)
    {
        vTaskDelay(pdMS_TO_TICKS(200)); // Give task time to exit
        s_vision_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Vision processing stopped");
    return ESP_OK;
}

esp_err_t vision_engine_get_result(vision_result_t *result)
{
    if (result == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(100)))
    {
        memcpy(result, &s_last_result, sizeof(vision_result_t));
        xSemaphoreGive(s_result_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

bool vision_engine_is_veto_active(void)
{
    bool veto = false;

    if (xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(10)))
    {
        veto = s_veto_active;
        xSemaphoreGive(s_result_mutex);
    }

    return veto;
}

camera_fb_t *vision_engine_get_frame(void)
{
    return esp_camera_fb_get();
}

void vision_engine_get_stats(float *avg_fps, float *avg_process_time_ms)
{
    if (avg_fps)
    {
        *avg_fps = (s_frame_counter > 0) ? (s_frame_counter / (esp_timer_get_time() / 1000000.0f)) : 0.0f;
    }

    if (avg_process_time_ms)
    {
        *avg_process_time_ms = (s_frame_counter > 0) ? (s_total_process_time_us / (float)s_frame_counter / 1000.0f) : 0.0f;
    }
}

void vision_engine_set_debug(bool enable)
{
    s_debug_enabled = enable;
    ESP_LOGI(TAG, "Debug visualization %s", enable ? "ENABLED" : "DISABLED");
}
