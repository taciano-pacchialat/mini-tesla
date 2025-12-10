/**
 * @file vision_engine.h
 * @brief Motor de visión mínimo para detectar obstáculos naranjas.
 */

#ifndef VISION_ENGINE_H
#define VISION_ENGINE_H

#include "esp_err.h"
#include <stdbool.h>
#include "../ws_client/ws_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool obstacle_detected;
    float distance_cm;
    bool reverse_only;
} vision_result_t;

esp_err_t vision_engine_init(void);
esp_err_t vision_engine_start(void);
esp_err_t vision_engine_stop(void);
esp_err_t vision_engine_get_result(vision_result_t *result);
bool vision_engine_is_veto_active(void);
bool vision_engine_command_allowed(control_command_t command);

#ifdef __cplusplus
}
#endif

#endif // VISION_ENGINE_H
