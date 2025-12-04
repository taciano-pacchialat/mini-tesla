/**
 * @file autonomous_task.h
 * @brief Autonomous control logic for target tracking with local veto system
 * 
 * Applies manual control commands coming from the dashboard and enforces
 * a local veto (from the onboard camera) to avoid collisions.
 */

#ifndef AUTONOMOUS_TASK_H
#define AUTONOMOUS_TASK_H

#include "esp_err.h"
#include "ws_client.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Control parameters
// Manual driving speeds
#define MANUAL_FORWARD_SPEED            180
#define MANUAL_BACKWARD_SPEED           160
#define MANUAL_TURN_SPEED               140

// Control states
typedef enum {
    STATE_IDLE = 0,
    STATE_FORWARD,
    STATE_BACKWARD,
    STATE_TURNING,
    STATE_EMERGENCY
} control_state_t;

/**
 * @brief Initialize autonomous control system
 * 
 * Must be called after motor_control_init().
 * 
 * @return ESP_OK on success
 */
esp_err_t autonomous_init(void);

/**
 * @brief Apply a manual command considering the local veto flag
 * 
 * This function fuses manual commands from the dashboard with the
 * local obstacle detection veto:
 *
 * - If local vision detects GREEN obstacle within safety distance,
 *   VETO is activated and forward motion is blocked
 * - Otherwise, applies the requested manual command
 * 
 * @param command Pointer to control message from dashboard
 * @param local_veto True if local vision detected obstacle
 * @return ESP_OK on success
 */
esp_err_t autonomous_process_with_veto(const control_message_t *command, bool local_veto);

/**
 * @brief Get current control state
 * 
 * @return Current state
 */
control_state_t autonomous_get_state(void);

/**
 * @brief Emergency stop - triggers EMERGENCY state
 * 
 * Called when connection is lost or critical error occurs.
 * 
 * @return ESP_OK on success
 */
esp_err_t autonomous_emergency_stop(void);

/**
 * @brief Get state as string for logging/reporting
 * 
 * @return String representation of current state
 */
const char* autonomous_state_to_string(control_state_t state);

#ifdef __cplusplus
}
#endif

#endif // AUTONOMOUS_TASK_H
