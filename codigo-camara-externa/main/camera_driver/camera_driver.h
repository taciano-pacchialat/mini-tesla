#ifndef CAMERA_DRIVER_H
#define CAMERA_DRIVER_H

#include "esp_err.h"
#include "esp_camera.h"

// Pines para ESP32-S3 (OV2640)
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK -1 // Módulo sin XCLK (usa oscilador interno)
#define CAM_PIN_SIOD 6
#define CAM_PIN_SIOC 5

#define CAM_PIN_D7 16
#define CAM_PIN_D6 17
#define CAM_PIN_D5 18
#define CAM_PIN_D4 12
#define CAM_PIN_D3 11
#define CAM_PIN_D2 10
#define CAM_PIN_D1 9
#define CAM_PIN_D0 8

#define CAM_PIN_VSYNC 7
#define CAM_PIN_HREF 13
#define CAM_PIN_PCLK 14

/**
 * @brief Initialize the camera with predefined pins and configuration
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_init(void);

/**
 * @brief Capture a frame from the camera
 *
 * @return Pointer to camera frame buffer, NULL on error
 *         Must be returned using camera_fb_return()
 */
camera_fb_t *camera_capture(void);

/**
 * @brief Return the frame buffer to the driver
 *
 * @param fb Pointer to frame buffer obtained from camera_capture()
 */
void camera_fb_return(camera_fb_t *fb);

/**
 * @brief Deinitialize the camera
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_deinit(void);

#endif // CAMERA_DRIVER_H
