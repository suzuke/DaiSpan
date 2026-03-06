#pragma once
#include "common/types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * Initialize the controller: creates command queue and spawns the controller task.
 */
esp_err_t thermostat_ctrl_init(void);

/* --- Non-blocking command API (called from HomeKit task) ---
 *
 * Send a command to the controller task. Returns immediately.
 * The controller task will process it asynchronously.
 */
bool thermostat_ctrl_set_power(bool on);
bool thermostat_ctrl_set_mode(uint8_t homekit_mode);
bool thermostat_ctrl_set_temperature(float temp);
bool thermostat_ctrl_set_fan_speed(uint8_t speed);
bool thermostat_ctrl_set_swing(swing_axis_t axis, bool enabled);

/* --- Cached state reads (lock-free, always returns immediately) ---
 *
 * These read from a cached copy updated by the controller task.
 */
bool     thermostat_ctrl_get_power(void);
uint8_t  thermostat_ctrl_get_mode(void);         /* returns HomeKit mode */
float    thermostat_ctrl_get_target_temp(void);
float    thermostat_ctrl_get_current_temp(void);
uint8_t  thermostat_ctrl_get_fan_speed(void);
bool     thermostat_ctrl_get_swing(swing_axis_t axis);
bool     thermostat_ctrl_is_healthy(void);
uint32_t thermostat_ctrl_get_error_count(void);
