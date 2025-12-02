/**
 * @file autonomous_task.c
 * @brief Autonomous control implementation
 */

#include "autonomous_task.h"
#include "motor_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>

static const char *TAG = "[Control]";

// Current control state
static control_state_t s_current_state = STATE_SEARCHING;
static SemaphoreHandle_t s_state_mutex = NULL;

/**
 * @brief Set current state (thread-safe)
 */
static void set_state(control_state_t new_state)
{
    if (s_state_mutex && xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)))
    {
        if (s_current_state != new_state)
        {
            ESP_LOGI(TAG, "State transition: %s -> %s",
                     autonomous_state_to_string(s_current_state),
                     autonomous_state_to_string(new_state));
            s_current_state = new_state;
        }
        xSemaphoreGive(s_state_mutex);
    }
}

/**
 * @brief Search behavior - rotate slowly to find target
 */
static void behavior_search(void)
{
    ESP_LOGD(TAG, "Executing SEARCH behavior");
    // Rotate in place: left motor forward, right motor reverse
    motor_set_speed(SEARCH_TURN_SPEED, -SEARCH_TURN_SPEED);
    set_state(STATE_SEARCHING);
}

/**
 * @brief Stop behavior - obstacle too close
 */
static void behavior_stop(void)
{
    ESP_LOGW(TAG, "Executing STOP behavior - obstacle too close!");
    motor_set_speed(0, 0);
    set_state(STATE_STOPPED);
}

/**
 * @brief Follow behavior - track target using proportional control
 */
static void behavior_follow(float distance_cm, float angle_deg)
{
    ESP_LOGD(TAG, "Executing FOLLOW behavior: distance=%.1f cm, angle=%.1f deg",
             distance_cm, angle_deg);

    // Proportional control based on angle
    // Positive angle = target to the right, increase right motor to turn right
    // Negative angle = target to the left, increase left motor to turn left

    int base_speed = BASE_SPEED_FOLLOW;
    int correction = (int)(angle_deg * ANGLE_CORRECTION_FACTOR);

    // Calculate motor speeds with differential steering
    int left_speed = base_speed - correction;
    int right_speed = base_speed + correction;

    // Clamp speeds to valid range
    if (left_speed > 255)
        left_speed = 255;
    if (left_speed < -255)
        left_speed = -255;
    if (right_speed > 255)
        right_speed = 255;
    if (right_speed < -255)
        right_speed = -255;

    ESP_LOGD(TAG, "Motor commands: Left=%d, Right=%d", left_speed, right_speed);
    motor_set_speed(left_speed, right_speed);
    set_state(STATE_FOLLOWING);
}

/**
 * @brief Emergency behavior - stop immediately
 */
static void behavior_emergency(void)
{
    ESP_LOGE(TAG, "EMERGENCY STOP - connection lost or critical error");
    motor_emergency_stop();
    set_state(STATE_EMERGENCY);
}

esp_err_t autonomous_init(void)
{
    ESP_LOGI(TAG, "Initializing autonomous control...");

    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_FAIL;
    }

    // Initialize in search mode
    s_current_state = STATE_SEARCHING;

    ESP_LOGI(TAG, "Autonomous control initialized - starting in SEARCH mode");
    return ESP_OK;
}

esp_err_t autonomous_process_telemetry(const telemetry_data_t *telemetry)
{
    if (telemetry == NULL)
    {
        ESP_LOGW(TAG, "Null telemetry data received");
        return ESP_FAIL;
    }

    // Log telemetry data
    ESP_LOGI(TAG, "Telemetry: detected=%d, object=%s, distance=%.1f cm, angle=%.1f deg",
             telemetry->detected, telemetry->object_type,
             telemetry->distance_cm, telemetry->angle_deg);

    // Decision logic based on telemetry
    if (!telemetry->detected)
    {
        // No target detected - search for it
        behavior_search();
    }
    else
    {
        // Target detected - check distance
        if (telemetry->distance_cm < DISTANCE_STOP_THRESHOLD_CM)
        {
            // Too close - stop to avoid collision
            behavior_stop();
        }
        else if (telemetry->distance_cm <= DISTANCE_FOLLOW_MAX_CM)
        {
            // Within follow range - track the target
            behavior_follow(telemetry->distance_cm, telemetry->angle_deg);
        }
        else
        {
            // Target too far - search mode
            behavior_search();
        }
    }

    return ESP_OK;
}

control_state_t autonomous_get_state(void)
{
    control_state_t state = STATE_SEARCHING;
    if (s_state_mutex && xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(10)))
    {
        state = s_current_state;
        xSemaphoreGive(s_state_mutex);
    }
    return state;
}

esp_err_t autonomous_emergency_stop(void)
{
    behavior_emergency();
    return ESP_OK;
}

const char *autonomous_state_to_string(control_state_t state)
{
    switch (state)
    {
    case STATE_STOPPED:
        return "STOPPED";
    case STATE_FOLLOWING:
        return "FOLLOWING";
    case STATE_SEARCHING:
        return "SEARCHING";
    case STATE_EMERGENCY:
        return "EMERGENCY";
    default:
        return "UNKNOWN";
    }
}
