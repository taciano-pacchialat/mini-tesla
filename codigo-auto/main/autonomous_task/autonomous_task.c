/**
 * @file autonomous_task.c
 * @brief Autonomous control implementation
 */

#include "autonomous_task.h"
#include "motor_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "[Control]";

// Current control state
static control_state_t s_current_state = STATE_IDLE;
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

static void apply_stop(void)
{
    motor_set_speed(0, 0);
    set_state(STATE_IDLE);
}

static void apply_forward(void)
{
    motor_set_speed(MANUAL_FORWARD_SPEED, MANUAL_FORWARD_SPEED);
    set_state(STATE_FORWARD);
}

static void apply_backward(void)
{
    motor_set_speed(-MANUAL_BACKWARD_SPEED, -MANUAL_BACKWARD_SPEED);
    set_state(STATE_BACKWARD);
}

static void apply_turn_left(void)
{
    motor_set_speed(-MANUAL_TURN_SPEED, MANUAL_TURN_SPEED);
    set_state(STATE_TURNING);
}

static void apply_turn_right(void)
{
    motor_set_speed(MANUAL_TURN_SPEED, -MANUAL_TURN_SPEED);
    set_state(STATE_TURNING);
}

static void apply_emergency(void)
{
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

    s_current_state = STATE_IDLE;

    ESP_LOGI(TAG, "Manual control initialized - awaiting commands");
    return ESP_OK;
}

esp_err_t autonomous_process_with_veto(const control_message_t *command, bool local_veto)
{
    if (!command)
    {
        ESP_LOGW(TAG, "Null control command received");
        apply_stop();
        return ESP_FAIL;
    }

    if (local_veto && command->command == CONTROL_CMD_FORWARD)
    {
        ESP_LOGW(TAG, "Local veto active: blocking forward motion");
        apply_stop();
        return ESP_OK;
    }

    switch (command->command)
    {
    case CONTROL_CMD_FORWARD:
        apply_forward();
        break;
    case CONTROL_CMD_BACKWARD:
        apply_backward();
        break;
    case CONTROL_CMD_LEFT:
        apply_turn_left();
        break;
    case CONTROL_CMD_RIGHT:
        apply_turn_right();
        break;
    case CONTROL_CMD_STOP:
    default:
        apply_stop();
        break;
    }

    return ESP_OK;
}

control_state_t autonomous_get_state(void)
{
    control_state_t state = STATE_IDLE;
    if (s_state_mutex && xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(10)))
    {
        state = s_current_state;
        xSemaphoreGive(s_state_mutex);
    }
    return state;
}

esp_err_t autonomous_emergency_stop(void)
{
    apply_emergency();
    return ESP_OK;
}

const char *autonomous_state_to_string(control_state_t state)
{
    switch (state)
    {
    case STATE_IDLE:
        return "IDLE";
    case STATE_FORWARD:
        return "FORWARD";
    case STATE_BACKWARD:
        return "BACKWARD";
    case STATE_TURNING:
        return "TURNING";
    case STATE_EMERGENCY:
        return "EMERGENCY";
    default:
        return "UNKNOWN";
    }
}
