/**
 * @file vision_engine.h
 * @brief Local vision system for ESP32-CAM obstacle detection
 * 
 * Implements:
 * - OV2640 camera initialization with RGB565 format
 * - HSV color space conversion for robust green obstacle detection
 * - Distance estimation using pinhole camera model
 * - Thread-safe veto system for collision avoidance
 * 
 * Architecture:
 * - Runs on Core 1 (Application CPU) as high-priority FreeRTOS task
 * - Zero-copy frame buffer access for performance
 * - Uses C-style OpenCV macros (CV_*) for ESP32 compatibility
 */

#ifndef VISION_ENGINE_H
#define VISION_ENGINE_H

#include "esp_err.h"
#include "esp_camera.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Vision detection result structure
typedef struct {
    bool obstacle_detected;      // True if green obstacle found
    float distance_cm;           // Estimated distance in centimeters
    int centroid_x;              // Object center X coordinate (pixels)
    int centroid_y;              // Object center Y coordinate (pixels)
    int contour_area;            // Largest contour area (pixels²)
    uint32_t frame_count;        // Processed frames counter
    uint32_t processing_time_ms; // Last frame processing time
} vision_result_t;

/**
 * @brief Initialize vision engine and camera
 * 
 * - Configures OV2640 camera with AI Thinker pinout
 * - Sets QVGA resolution (320x240) and RGB565 format
 * - Allocates frame buffers in external PSRAM
 * - Creates vision processing task on Core 1
 * 
 * @return ESP_OK on success, ESP_FAIL if camera init fails
 */
esp_err_t vision_engine_init(void);

/**
 * @brief Start vision processing task
 * 
 * Launches FreeRTOS task that continuously:
 * 1. Captures frame from camera
 * 2. Converts RGB565 -> BGR -> HSV
 * 3. Detects green obstacles
 * 4. Calculates distance
 * 5. Updates veto flag
 * 
 * @return ESP_OK on success
 */
esp_err_t vision_engine_start(void);

/**
 * @brief Stop vision processing task
 * 
 * @return ESP_OK on success
 */
esp_err_t vision_engine_stop(void);

/**
 * @brief Get current vision detection result (thread-safe)
 * 
 * Returns a copy of the latest detection data.
 * Safe to call from any task.
 * 
 * @param[out] result Pointer to store detection result
 * @return ESP_OK on success
 */
esp_err_t vision_engine_get_result(vision_result_t *result);

/**
 * @brief Check if local veto is active (thread-safe)
 * 
 * Returns true if a green obstacle is detected within
 * the safety threshold distance (VETO_DISTANCE_THRESHOLD_CM).
 * This should block forward movement commands.
 * 
 * @return true if veto active (obstacle too close), false otherwise
 */
bool vision_engine_is_veto_active(void);

/**
 * @brief Get last captured frame buffer for streaming
 * 
 * Returns the camera frame buffer. CRITICAL: caller must
 * call esp_camera_fb_return() when done to avoid memory leak.
 * 
 * @return Pointer to camera frame buffer, or NULL if error
 */
camera_fb_t* vision_engine_get_frame(void);

/**
 * @brief Get vision engine statistics
 * 
 * @param[out] avg_fps Average frames per second
 * @param[out] avg_process_time_ms Average processing time in ms
 */
void vision_engine_get_stats(float *avg_fps, float *avg_process_time_ms);

/**
 * @brief Enable/disable debug visualization
 * 
 * When enabled, draws bounding boxes on detected objects.
 * WARNING: This significantly reduces FPS. Use only for debugging.
 * 
 * @param enable true to enable debug drawing
 */
void vision_engine_set_debug(bool enable);

#ifdef __cplusplus
}
#endif

#endif // VISION_ENGINE_H
