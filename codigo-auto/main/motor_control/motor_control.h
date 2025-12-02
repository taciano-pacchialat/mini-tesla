/**
 * @file motor_control.h
 * @brief Motor Control using MCPWM for differential drive robot
 *
 * Provides PWM-based speed control for left and right motors.
 * Uses ESP32's MCPWM (Motor Control PWM) peripheral for precise control.
 */

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Motor GPIO pin definitions (configurable via menuconfig or here)
#define MOTOR_LEFT_PWM_GPIO 25  // PWM signal for left motor
#define MOTOR_LEFT_DIR_GPIO 26  // Direction control for left motor
#define MOTOR_RIGHT_PWM_GPIO 27 // PWM signal for right motor
#define MOTOR_RIGHT_DIR_GPIO 14 // Direction control for right motor

// PWM configuration
#define MOTOR_PWM_FREQ_HZ 1000            // 1 kHz PWM frequency
#define MOTOR_TIMER_RESOLUTION_HZ 1000000 // 1 MHz timer resolution

// Speed limits
#define MOTOR_SPEED_MIN  -255 // Full reverse
#define MOTOR_SPEED_MAX  255  // Full forward
#define MOTOR_SPEED_STOP 0    // Stop

    /**
     * @brief Initialize motor control system
     *
     * Configures MCPWM peripheral and GPIO pins for both motors.
     * Motors are initialized in stopped state.
     *
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t motor_control_init(void);

    /**
     * @brief Set speed for both motors
     *
     * @param left_speed Speed for left motor (-100 to 100)
     *                   Negative = reverse, Positive = forward
     * @param right_speed Speed for right motor (-100 to 100)
     * @return ESP_OK on success
     */
    esp_err_t motor_set_speed(int left_speed, int right_speed);

    /**
     * @brief Set speed for left motor only
     *
     * @param speed Speed (-100 to 100)
     * @return ESP_OK on success
     */
    esp_err_t motor_set_left(int speed);

    /**
     * @brief Set speed for right motor only
     *
     * @param speed Speed (-100 to 100)
     * @return ESP_OK on success
     */
    esp_err_t motor_set_right(int speed);

    /**
     * @brief Emergency stop - immediately stop both motors
     *
     * @return ESP_OK on success
     */
    esp_err_t motor_emergency_stop(void);

    /**
     * @brief Get current left motor speed
     *
     * @return Current speed (-100 to 100)
     */
    int motor_get_left_speed(void);

    /**
     * @brief Get current right motor speed
     *
     * @return Current speed (-100 to 100)
     */
    int motor_get_right_speed(void);

    /**
     * @brief Deinitialize motor control
     *
     * Stops motors and releases MCPWM resources.
     *
     * @return ESP_OK on success
     */
    esp_err_t motor_control_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // MOTOR_CONTROL_H
