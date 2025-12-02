/**
 * @file motor_control.c
 * @brief Motor Control implementation for L298N using LEDC PWM
 *
 * L298N Control Logic:
 * - Forward:  IN1=HIGH, IN2=LOW,  ENA=PWM
 * - Reverse:  IN1=LOW,  IN2=HIGH, ENA=PWM
 * - Brake:    IN1=HIGH, IN2=HIGH, ENA=HIGH (or LOW)
 * - Coast:    IN1=LOW,  IN2=LOW,  ENA=any
 */

#include "motor_control.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "[Motors]";

// Motor speed state
static int s_left_speed = 0;
static int s_right_speed = 0;

// Mutex for thread-safe motor control
static SemaphoreHandle_t s_motor_mutex = NULL;

/**
 * @brief Clamp speed to valid range [-255, 255]
 */
static inline int clamp_speed(int speed)
{
    if (speed > MOTOR_SPEED_MAX)
        return MOTOR_SPEED_MAX;
    if (speed < MOTOR_SPEED_MIN)
        return MOTOR_SPEED_MIN;
    return speed;
}

/**
 * @brief Set L298N motor direction and speed
 *
 * @param in1_gpio First direction pin
 * @param in2_gpio Second direction pin
 * @param pwm_channel LEDC channel for PWM
 * @param speed Motor speed (-255 to 255)
 */
static esp_err_t apply_motor_speed_l298n(int in1_gpio, int in2_gpio,
                                         ledc_channel_t pwm_channel, int speed)
{
    speed = clamp_speed(speed);

    uint32_t duty = abs(speed); // 0-255

    if (speed > 0)
    {
        // Forward: IN1=HIGH, IN2=LOW
        gpio_set_level(in1_gpio, 1);
        gpio_set_level(in2_gpio, 0);
    }
    else if (speed < 0)
    {
        // Reverse: IN1=LOW, IN2=HIGH
        gpio_set_level(in1_gpio, 0);
        gpio_set_level(in2_gpio, 1);
    }
    else
    {
        // Stop (coast): IN1=LOW, IN2=LOW
        gpio_set_level(in1_gpio, 0);
        gpio_set_level(in2_gpio, 0);
        duty = 0;
    }

    // Set PWM duty cycle
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, pwm_channel, duty);
    if (err == ESP_OK)
    {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, pwm_channel);
    }

    return err;
}

esp_err_t motor_control_init(void)
{
    ESP_LOGI(TAG, "Initializing L298N motor control...");

    s_motor_mutex = xSemaphoreCreateMutex();
    if (!s_motor_mutex)
    {
        ESP_LOGE(TAG, "Failed to create motor mutex");
        return ESP_FAIL;
    }

    // --- Configure Direction GPIOs (IN1, IN2, IN3, IN4) ---
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << MOTOR_LEFT_IN1_GPIO) |
                        (1ULL << MOTOR_LEFT_IN2_GPIO) |
                        (1ULL << MOTOR_RIGHT_IN1_GPIO) |
                        (1ULL << MOTOR_RIGHT_IN2_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Initialize all direction pins to LOW (coast)
    gpio_set_level(MOTOR_LEFT_IN1_GPIO, 0);
    gpio_set_level(MOTOR_LEFT_IN2_GPIO, 0);
    gpio_set_level(MOTOR_RIGHT_IN1_GPIO, 0);
    gpio_set_level(MOTOR_RIGHT_IN2_GPIO, 0);

    // --- Configure LEDC Timer ---
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT, // 0-255
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // --- Configure Left Motor PWM (ENA - GPIO 4) ---
    ledc_channel_config_t ledc_channel_left = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = MOTOR_LEFT_PWM_GPIO,
        .duty = 0, // Start stopped
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_left));

    // --- Configure Right Motor PWM (ENB - GPIO 12) ---
    ledc_channel_config_t ledc_channel_right = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = MOTOR_RIGHT_PWM_GPIO,
        .duty = 0, // Start stopped
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_right));

    ESP_LOGI(TAG, "Motor control initialized successfully");
    ESP_LOGI(TAG, "Left motor:  ENA=GPIO%d, IN1=GPIO%d, IN2=GPIO%d",
             MOTOR_LEFT_PWM_GPIO, MOTOR_LEFT_IN1_GPIO, MOTOR_LEFT_IN2_GPIO);
    ESP_LOGI(TAG, "Right motor: ENB=GPIO%d, IN3=GPIO%d, IN4=GPIO%d",
             MOTOR_RIGHT_PWM_GPIO, MOTOR_RIGHT_IN1_GPIO, MOTOR_RIGHT_IN2_GPIO);

    return ESP_OK;
}

esp_err_t motor_set_speed(int left_speed, int right_speed)
{
    if (!s_motor_mutex)
        return ESP_FAIL;

    if (xSemaphoreTake(s_motor_mutex, pdMS_TO_TICKS(100)))
    {
        esp_err_t err_left = apply_motor_speed_l298n(
            MOTOR_LEFT_IN1_GPIO, MOTOR_LEFT_IN2_GPIO,
            LEDC_CHANNEL_0, left_speed);

        esp_err_t err_right = apply_motor_speed_l298n(
            MOTOR_RIGHT_IN1_GPIO, MOTOR_RIGHT_IN2_GPIO,
            LEDC_CHANNEL_1, right_speed);

        if (err_left == ESP_OK && err_right == ESP_OK)
        {
            s_left_speed = clamp_speed(left_speed);
            s_right_speed = clamp_speed(right_speed);
        }

        xSemaphoreGive(s_motor_mutex);
        return (err_left == ESP_OK && err_right == ESP_OK) ? ESP_OK : ESP_FAIL;
    }
    return ESP_FAIL;
}

esp_err_t motor_set_left(int speed)
{
    if (!s_motor_mutex)
        return ESP_FAIL;

    if (xSemaphoreTake(s_motor_mutex, pdMS_TO_TICKS(100)))
    {
        esp_err_t err = apply_motor_speed_l298n(
            MOTOR_LEFT_IN1_GPIO, MOTOR_LEFT_IN2_GPIO,
            LEDC_CHANNEL_0, speed);

        if (err == ESP_OK)
        {
            s_left_speed = clamp_speed(speed);
        }
        xSemaphoreGive(s_motor_mutex);
        return err;
    }
    return ESP_FAIL;
}

esp_err_t motor_set_right(int speed)
{
    if (!s_motor_mutex)
        return ESP_FAIL;

    if (xSemaphoreTake(s_motor_mutex, pdMS_TO_TICKS(100)))
    {
        esp_err_t err = apply_motor_speed_l298n(
            MOTOR_RIGHT_IN1_GPIO, MOTOR_RIGHT_IN2_GPIO,
            LEDC_CHANNEL_1, speed);

        if (err == ESP_OK)
        {
            s_right_speed = clamp_speed(speed);
        }
        xSemaphoreGive(s_motor_mutex);
        return err;
    }
    return ESP_FAIL;
}

esp_err_t motor_emergency_stop(void)
{
    if (!s_motor_mutex)
        return ESP_FAIL;

    if (xSemaphoreTake(s_motor_mutex, pdMS_TO_TICKS(100)))
    {
        // Hard brake: IN1=HIGH, IN2=HIGH for both motors
        gpio_set_level(MOTOR_LEFT_IN1_GPIO, 1);
        gpio_set_level(MOTOR_LEFT_IN2_GPIO, 1);
        gpio_set_level(MOTOR_RIGHT_IN1_GPIO, 1);
        gpio_set_level(MOTOR_RIGHT_IN2_GPIO, 1);

        // Set PWM to max for hard brake
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 255);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 255);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

        s_left_speed = 0;
        s_right_speed = 0;

        xSemaphoreGive(s_motor_mutex);
        ESP_LOGW(TAG, "Emergency stop activated");
        return ESP_OK;
    }
    return ESP_FAIL;
}

void motor_get_speeds(int *left_speed, int *right_speed)
{
    if (!s_motor_mutex)
        return;

    if (xSemaphoreTake(s_motor_mutex, pdMS_TO_TICKS(10)))
    {
        if (left_speed)
            *left_speed = s_left_speed;
        if (right_speed)
            *right_speed = s_right_speed;
        xSemaphoreGive(s_motor_mutex);
    }
}
