/**
 * @file motor_control.c
 * @brief Motor Control implementation using the new MCPWM driver
 */

#include "motor_control.h"
#include "driver/mcpwm_prelude.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "[Motors]";

// Motor speed state
static int s_left_speed = 0;
static int s_right_speed = 0;

// Handles for MCPWM resources
static mcpwm_timer_handle_t s_left_timer = NULL;
static mcpwm_timer_handle_t s_right_timer = NULL;
static mcpwm_oper_handle_t s_left_operator = NULL;
static mcpwm_oper_handle_t s_right_operator = NULL;
static mcpwm_cmpr_handle_t s_left_comparator = NULL;
static mcpwm_cmpr_handle_t s_right_comparator = NULL;
static mcpwm_gen_handle_t s_left_generator = NULL;
static mcpwm_gen_handle_t s_right_generator = NULL;

// Mutex for thread-safe motor control
static SemaphoreHandle_t s_motor_mutex = NULL;

/**
 * @brief Clamp speed to valid range [-100, 100]
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
 * @brief Apply speed to a single motor using the new MCPWM driver
 */
static esp_err_t apply_motor_speed(mcpwm_cmpr_handle_t comparator, int dir_gpio, int speed)
{
    speed = clamp_speed(speed);

    // Determine direction and duty cycle
    bool forward = (speed >= 0);
    uint32_t period_ticks = MOTOR_TIMER_RESOLUTION_HZ / MOTOR_PWM_FREQ_HZ;
    uint32_t duty_ticks = (abs(speed) * period_ticks) / 255;

    // Set direction GPIO
    gpio_set_level(dir_gpio, forward ? 1 : 0);

    // Set PWM duty cycle by updating the comparator value
    return mcpwm_comparator_set_compare_value(comparator, duty_ticks);
}

esp_err_t motor_control_init(void)
{
    ESP_LOGI(TAG, "Initializing motor control with new MCPWM driver...");

    s_motor_mutex = xSemaphoreCreateMutex();
    if (!s_motor_mutex)
    {
        ESP_LOGE(TAG, "Failed to create motor mutex");
        return ESP_FAIL;
    }

    // --- Configure Direction GPIOs ---
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << MOTOR_LEFT_DIR_GPIO) | (1ULL << MOTOR_RIGHT_DIR_GPIO),
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(MOTOR_LEFT_DIR_GPIO, 1);
    gpio_set_level(MOTOR_RIGHT_DIR_GPIO, 1);

    // --- Create Left Motor MCPWM resources ---
    ESP_LOGI(TAG, "Creating left motor timer");
    mcpwm_timer_config_t timer_config_left = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MOTOR_TIMER_RESOLUTION_HZ,
        .period_ticks = MOTOR_TIMER_RESOLUTION_HZ / MOTOR_PWM_FREQ_HZ,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config_left, &s_left_timer));

    ESP_LOGI(TAG, "Creating left motor operator");
    mcpwm_operator_config_t oper_config_left = {.group_id = 0};
    ESP_ERROR_CHECK(mcpwm_new_operator(&oper_config_left, &s_left_operator));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(s_left_operator, s_left_timer));

    ESP_LOGI(TAG, "Creating left motor comparator");
    mcpwm_comparator_config_t compare_config_left = {.flags.update_cmp_on_tez = true};
    ESP_ERROR_CHECK(mcpwm_new_comparator(s_left_operator, &compare_config_left, &s_left_comparator));

    ESP_LOGI(TAG, "Creating left motor generator");
    mcpwm_generator_config_t gen_config_left = {.gen_gpio_num = MOTOR_LEFT_PWM_GPIO};
    ESP_ERROR_CHECK(mcpwm_new_generator(s_left_operator, &gen_config_left, &s_left_generator));

    // Set initial duty cycle to 0
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(s_left_comparator, 0));

    // Set generator actions on timer events
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(s_left_generator,
                                                              MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(s_left_generator,
                                                                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_left_comparator, MCPWM_GEN_ACTION_LOW)));

    // --- Create Right Motor MCPWM resources ---
    ESP_LOGI(TAG, "Creating right motor timer");
    mcpwm_timer_config_t timer_config_right = {
        .group_id = 1, // Use group 1 for the second motor
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MOTOR_TIMER_RESOLUTION_HZ,
        .period_ticks = MOTOR_TIMER_RESOLUTION_HZ / MOTOR_PWM_FREQ_HZ,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config_right, &s_right_timer));

    ESP_LOGI(TAG, "Creating right motor operator");
    mcpwm_operator_config_t oper_config_right = {.group_id = 1};
    ESP_ERROR_CHECK(mcpwm_new_operator(&oper_config_right, &s_right_operator));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(s_right_operator, s_right_timer));

    ESP_LOGI(TAG, "Creating right motor comparator");
    mcpwm_comparator_config_t compare_config_right = {.flags.update_cmp_on_tez = true};
    ESP_ERROR_CHECK(mcpwm_new_comparator(s_right_operator, &compare_config_right, &s_right_comparator));

    ESP_LOGI(TAG, "Creating right motor generator");
    mcpwm_generator_config_t gen_config_right = {.gen_gpio_num = MOTOR_RIGHT_PWM_GPIO};
    ESP_ERROR_CHECK(mcpwm_new_generator(s_right_operator, &gen_config_right, &s_right_generator));

    // Set initial duty cycle to 0
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(s_right_comparator, 0));

    // Set generator actions
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(s_right_generator,
                                                              MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(s_right_generator,
                                                                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_right_comparator, MCPWM_GEN_ACTION_LOW)));

    // --- Start Timers ---
    ESP_LOGI(TAG, "Starting MCPWM timers");
    ESP_ERROR_CHECK(mcpwm_timer_enable(s_left_timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(s_left_timer, MCPWM_TIMER_START_NO_STOP));
    ESP_ERROR_CHECK(mcpwm_timer_enable(s_right_timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(s_right_timer, MCPWM_TIMER_START_NO_STOP));

    motor_emergency_stop();

    ESP_LOGI(TAG, "Motor control initialized successfully");
    ESP_LOGI(TAG, "Left motor: PWM=GPIO%d, DIR=GPIO%d", MOTOR_LEFT_PWM_GPIO, MOTOR_LEFT_DIR_GPIO);
    ESP_LOGI(TAG, "Right motor: PWM=GPIO%d, DIR=GPIO%d", MOTOR_RIGHT_PWM_GPIO, MOTOR_RIGHT_DIR_GPIO);

    return ESP_OK;
}

esp_err_t motor_set_speed(int left_speed, int right_speed)
{
    if (!s_motor_mutex)
        return ESP_FAIL;

    if (xSemaphoreTake(s_motor_mutex, pdMS_TO_TICKS(100)))
    {
        esp_err_t err_left = apply_motor_speed(s_left_comparator, MOTOR_LEFT_DIR_GPIO, left_speed);
        esp_err_t err_right = apply_motor_speed(s_right_comparator, MOTOR_RIGHT_DIR_GPIO, right_speed);

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
        esp_err_t err = apply_motor_speed(s_left_comparator, MOTOR_LEFT_DIR_GPIO, speed);
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
        esp_err_t err = apply_motor_speed(s_right_comparator, MOTOR_RIGHT_DIR_GPIO, speed);
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
    ESP_LOGW(TAG, "EMERGENCY STOP activated");

    // This function should be safe to call even before the mutex is created
    if (s_left_generator && s_right_generator)
    {
        if (xSemaphoreTake(s_motor_mutex, pdMS_TO_TICKS(100)))
        {
            // Set PWM duty cycle to 0 to stop motors
            mcpwm_comparator_set_compare_value(s_left_comparator, 0);
            mcpwm_comparator_set_compare_value(s_right_comparator, 0);

            s_left_speed = 0;
            s_right_speed = 0;
            xSemaphoreGive(s_motor_mutex);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to acquire mutex for emergency stop");
            return ESP_FAIL;
        }
    }
    else
    {
        // If called during init before generators are ready, just log it.
        // The init sequence already sets the duty to 0.
        ESP_LOGI(TAG, "Emergency stop called during early initialization.");
    }

    return ESP_OK;
}

int motor_get_left_speed(void)
{
    return s_left_speed;
}

int motor_get_right_speed(void)
{
    return s_right_speed;
}

esp_err_t motor_control_deinit(void)
{
    ESP_LOGI(TAG, "De-initializing motor control");
    if (s_motor_mutex)
    {
        xSemaphoreTake(s_motor_mutex, portMAX_DELAY);

        // Stop timers
        mcpwm_timer_start_stop(s_left_timer, MCPWM_TIMER_STOP_EMPTY);
        mcpwm_timer_disable(s_left_timer);
        mcpwm_timer_start_stop(s_right_timer, MCPWM_TIMER_STOP_EMPTY);
        mcpwm_timer_disable(s_right_timer);

        // Delete resources
        mcpwm_del_generator(s_left_generator);
        mcpwm_del_comparator(s_left_comparator);
        mcpwm_del_operator(s_left_operator);
        mcpwm_del_timer(s_left_timer);

        mcpwm_del_generator(s_right_generator);
        mcpwm_del_comparator(s_right_comparator);
        mcpwm_del_operator(s_right_operator);
        mcpwm_del_timer(s_right_timer);

        xSemaphoreGive(s_motor_mutex);
        vSemaphoreDelete(s_motor_mutex);
        s_motor_mutex = NULL;
    }
    return ESP_OK;
}
