/**
 * @file hardware_config.h
 * @brief Hardware pin definitions for ESP32-CAM (AI Thinker) module
 *
 * CRITICAL: This file defines the exact GPIO mapping for the ESP32-CAM.
 * The camera and PSRAM occupy most GPIOs. Motors use SD card pins (12-15).
 *
 * Hardware: ESP32 (not S3), 4MB PSRAM (QSPI), OV2640 camera, L298N motor driver
 */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#ifdef __cplusplus
extern "C"
{
#endif

// ============================================================================
// CAMERA PINS - AI Thinker ESP32-CAM Standard Configuration
// ============================================================================
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1 // Not connected
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26 // I2C SDA
#define CAM_PIN_SIOC 27 // I2C SCL

#define CAM_FLASH_LED_GPIO 4 // On-board white flash LED (active HIGH)

// Camera data pins (parallel interface)
#define CAM_PIN_Y9 35
#define CAM_PIN_Y8 34
#define CAM_PIN_Y7 39
#define CAM_PIN_Y6 36
#define CAM_PIN_Y5 21
#define CAM_PIN_Y4 19
#define CAM_PIN_Y3 18
#define CAM_PIN_Y2 5

// Camera sync pins
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

// ============================================================================
// MOTOR PINS - Configuración para L298N Motor Driver
// ============================================================================
// Motor A (Izquierdo)
#define MOTOR_LEFT_PWM 33 // GPIO33 - ENA (PWM) Motor A (keeps flash LED free)
#define MOTOR_LEFT_IN1 2  // GPIO 2  - IN1 Motor A
#define MOTOR_LEFT_IN2 14 // GPIO 14 - IN2 Motor A

// Motor B (Derecho)
#define MOTOR_RIGHT_PWM 12 // GPIO 12 - ENB (PWM) Motor B
#define MOTOR_RIGHT_IN1 15 // GPIO 15 - IN3 Motor B
#define MOTOR_RIGHT_IN2 13 // GPIO 13 - IN4 Motor B

// LEDC PWM configuration
#define MOTOR_LEDC_TIMER LEDC_TIMER_0
#define MOTOR_LEDC_MODE LEDC_LOW_SPEED_MODE
#define MOTOR_LEDC_CHANNEL_L LEDC_CHANNEL_0
#define MOTOR_LEDC_CHANNEL_R LEDC_CHANNEL_1
#define MOTOR_LEDC_DUTY_RES LEDC_TIMER_8_BIT // 0-255 duty cycle
#define MOTOR_LEDC_FREQUENCY 1000            // 1 kHz PWM

// ============================================================================
// CAMERA CONFIGURATION
// ============================================================================
#define CAM_FRAME_SIZE FRAMESIZE_QVGA      // 320x240 (optimal for ESP32)
#define CAM_PIXEL_FORMAT PIXFORMAT_RGB565  // Raw 16-bit for processing
#define CAM_FB_COUNT 2                     // Double buffering
#define CAM_JPEG_QUALITY 12                // For streaming (if needed)
#define CAM_FB_LOCATION CAMERA_FB_IN_PSRAM // CRITICAL: Use external PSRAM

// ============================================================================
// VISION PARAMETERS - HSV Color Ranges for Obstacle Detection
// ============================================================================

// GREEN obstacle detection (HSV in OpenCV scale: H=0-180, S=0-255, V=0-255)
#define HSV_GREEN_H_MIN 40  // Green hue starts ~40°
#define HSV_GREEN_H_MAX 80  // Green hue ends ~80°
#define HSV_GREEN_S_MIN 50  // Minimum saturation (avoid pale colors)
#define HSV_GREEN_S_MAX 255 // Maximum saturation
#define HSV_GREEN_V_MIN 50  // Minimum brightness (avoid dark shadows)
#define HSV_GREEN_V_MAX 255 // Maximum brightness

// ORANGE target detection (autito naranja)
#define HSV_ORANGE_H_MIN 10
#define HSV_ORANGE_H_MAX 30
#define HSV_ORANGE_S_MIN 60
#define HSV_ORANGE_S_MAX 255
#define HSV_ORANGE_V_MIN 80
#define HSV_ORANGE_V_MAX 255

// Morphological filtering (noise removal)
#define MORPH_KERNEL_SIZE 3 // 3x3 kernel for erosion/dilation

// Contour filtering
#define MIN_CONTOUR_AREA 200        // Pixels - ignore small noise
#define MAX_CONTOUR_AREA_RATIO 0.5f // 50% of image - ignore light errors

// Distance estimation (pinhole camera model)
// Formula: distance = (real_width * focal_length) / pixel_width
#define KNOWN_OBJECT_WIDTH_CM 10.0f      // Real width of obstacle in cm
#define CAMERA_FOCAL_LENGTH_PX 400.0f    // Calibrated focal length (adjust experimentally)
#define VETO_DISTANCE_THRESHOLD_CM 25.0f // Stop if obstacle < 25cm

// ============================================================================
// MEMORY AND PERFORMANCE
// ============================================================================
#define IMAGE_WIDTH 320
#define IMAGE_HEIGHT 240
#define RGB565_BYTES_PER_PIXEL 2
#define BGR888_BYTES_PER_PIXEL 3

// Frame buffer size calculations
#define FRAME_SIZE_RGB565 (IMAGE_WIDTH * IMAGE_HEIGHT * RGB565_BYTES_PER_PIXEL) // 153.6 KB
#define FRAME_SIZE_BGR888 (IMAGE_WIDTH * IMAGE_HEIGHT * BGR888_BYTES_PER_PIXEL) // 230.4 KB

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_CONFIG_H
