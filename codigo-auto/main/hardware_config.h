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

//#define CAM_FLASH_LED_GPIO 4   // deja esta línea comentada o elimínala

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



// Habilita/deshabilita todos los logs del proyecto
#define PROJECT_LOG_ENABLED 0


// ============================================================================
// MOTOR PINS - Configuración para L298N Motor Driver
// ============================================================================
// Motor A (Izquierdo)
#define MOTOR_LEFT_PWM 1  // U0T (GPIO1) - ENA (PWM) Motor A
#define MOTOR_LEFT_IN1 2  // GPIO 2  - IN1 Motor A
#define MOTOR_LEFT_IN2 14 // GPIO 14 - IN2 Motor A

// Motor B (Derecho)
#define MOTOR_RIGHT_PWM 12 // GPIO 12 - ENB (PWM) Motor B
#define MOTOR_RIGHT_IN1 15 // GPIO 15 - IN3 Motor B
#define MOTOR_RIGHT_IN2 13 // GPIO 13 - IN4 Motor B

#define MOTOR_RAMP_ENABLED     1    // 1 = habilita rampas, 0 = directo
#define MOTOR_RAMP_STEP        20   // incremento/decremento por paso (0-255)
#define MOTOR_RAMP_INTERVAL_MS 20   // tiempo entre pasos en ms

// Control parameters
// Manual driving speeds
#define MANUAL_FORWARD_SPEED            200
#define MANUAL_BACKWARD_SPEED           200
#define MANUAL_TURN_SPEED               190


// ============================================================================
// CAMERA CONFIGURATION
// ============================================================================
#define CAM_PIXEL_FORMAT        PIXFORMAT_RGB565
#define CAM_FRAME_SIZE          FRAMESIZE_VGA      // mayor resolución manteniendo estabilidad
#define CAM_FB_COUNT            2
#define CAM_JPEG_QUALITY         8                  // menor compresión para streaming más nítido
#define STREAM_JPEG_QUALITY     25
#define STREAM_MIN_INTERVAL_US  (120 * 1000)
#define CAM_FREQ_HZ 20000000
// ============================================================================
// VISION PARAMETERS - HSV Color Ranges for Obstacle Detection
// ============================================================================


// Distance estimation (pinhole camera model)
// Formula: distance = (real_width * focal_length) / pixel_width
#define KNOWN_OBJECT_WIDTH_CM   10.0f
#define CAMERA_FOCAL_LENGTH_PX  320.0f             // Ajustado para objeto naranja tenue

// ============================================================================
// MEMORY AND PERFORMANCE
// ============================================================================
#define IMAGE_WIDTH 320
#define IMAGE_HEIGHT 240





#ifdef __cplusplus
}
#endif

#endif // HARDWARE_CONFIG_H
