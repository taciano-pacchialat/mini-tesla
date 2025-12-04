/**
 * @file motor_control.h
 * @brief Motor Control using LEDC for L298N motor driver
 *
 * Provides PWM-based speed control for left and right motors.
 * L298N requires: ENA/ENB (PWM) + IN1/IN2 (direction) per motor.
 */

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include "esp_err.h"
#include <stdbool.h>
#include "../hardware_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef MOTOR_LEFT_PWM_GPIO
#define MOTOR_LEFT_PWM_GPIO MOTOR_LEFT_PWM
#endif
#ifndef MOTOR_LEFT_IN1_GPIO
#define MOTOR_LEFT_IN1_GPIO MOTOR_LEFT_IN1
#endif
#ifndef MOTOR_LEFT_IN2_GPIO
#define MOTOR_LEFT_IN2_GPIO MOTOR_LEFT_IN2
#endif

#ifndef MOTOR_RIGHT_PWM_GPIO
#define MOTOR_RIGHT_PWM_GPIO MOTOR_RIGHT_PWM
#endif
#ifndef MOTOR_RIGHT_IN1_GPIO
#define MOTOR_RIGHT_IN1_GPIO MOTOR_RIGHT_IN1
#endif
#ifndef MOTOR_RIGHT_IN2_GPIO
#define MOTOR_RIGHT_IN2_GPIO MOTOR_RIGHT_IN2
#endif

// PWM configuration
#define MOTOR_PWM_FREQ_HZ 1000            // 1 kHz PWM frequency
#define MOTOR_TIMER_RESOLUTION_HZ 1000000 // 1 MHz timer resolution

// Speed limits
#define MOTOR_SPEED_MIN -255 // Full reverse
#define MOTOR_SPEED_MAX 255  // Full forward
#define MOTOR_SPEED_STOP 0   // Stop

    /**
     * @brief Initialize motor control system
     *
     * Configures LEDC PWM and GPIO pins for L298N motor driver.
     * Motors are initialized in stopped state.
     *
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t motor_control_init(void);

    /**
     * @brief Set speed for both motors
     *
     * @param left_speed Speed for left motor (-255 to 255)
     *                   Negative = reverse, Positive = forward
     * @param right_speed Speed for right motor (-255 to 255)
     * @return ESP_OK on success
     */
    esp_err_t motor_set_speed(int left_speed, int right_speed);

    /**
     * @brief Set speed for left motor only
     *
     * @param speed Speed (-255 to 255)
     * @return ESP_OK on success
     */
    esp_err_t motor_set_left(int speed);

    /**
     * @brief Set speed for right motor only
     *
     * @param speed Speed (-255 to 255)
     * @return ESP_OK on success
     */
    esp_err_t motor_set_right(int speed);

    /**
     * @brief Emergency stop - immediately stops both motors
     *
     * @return ESP_OK on success
     */
    esp_err_t motor_emergency_stop(void);

    /**
     * @brief Get current motor speeds
     *
     * @param left_speed Pointer to store left motor speed
     * @param right_speed Pointer to store right motor speed
     */
    void motor_get_speeds(int *left_speed, int *right_speed);

#ifdef __cplusplus
}
#endif

#endif // MOTOR_CONTROL_H
