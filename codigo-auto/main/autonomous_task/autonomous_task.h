/**
 * @file autonomous_task.h
 * @brief Autonomous control logic for target tracking
 * 
 * Implements reactive behavior based on telemetry:
 * - STOP: Obstacle too close (< 30cm)
 * - FOLLOW: Target detected, adjust trajectory using angle
 * - SEARCH: No target detected, rotate to find it
 */

#ifndef AUTONOMOUS_TASK_H
#define AUTONOMOUS_TASK_H

#include "esp_err.h"
#include "ws_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// Control parameters
#define DISTANCE_STOP_THRESHOLD_CM      30.0f   // Stop if closer than this
#define DISTANCE_FOLLOW_MAX_CM          100.0f  // Maximum follow distance
#define BASE_SPEED_FOLLOW               150     // Base speed when following
#define SEARCH_TURN_SPEED               80      // Speed during search rotation
#define ANGLE_CORRECTION_FACTOR         2.0f    // Proportional control gain

// Control states
typedef enum {
    STATE_STOPPED,      // Motors stopped (obstacle too close)
    STATE_FOLLOWING,    // Following detected target
    STATE_SEARCHING,    // Rotating to find target
    STATE_EMERGENCY     // Emergency stop (connection lost)
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
 * @brief Process telemetry and update motor commands
 * 
 * This is the main control loop function.
 * Should be called when new telemetry data arrives.
 * 
 * @param telemetry Pointer to telemetry data
 * @return ESP_OK on success
 */
esp_err_t autonomous_process_telemetry(const telemetry_data_t *telemetry);

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
