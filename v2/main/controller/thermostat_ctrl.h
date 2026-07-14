#pragma once
#include "common/types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define THERMOSTAT_CTRL_MAX_CONSECUTIVE_ERRORS 10

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

typedef struct {
    ac_status_t desired;
    ac_status_t observed;
    uint8_t target_homekit_mode;
    uint32_t consecutive_errors;
    uint32_t dropped_commands;
    bool pending;
} thermostat_snapshot_t;

/* Copy a coherent controller snapshot under a short critical section. */
bool thermostat_ctrl_get_snapshot(thermostat_snapshot_t *snapshot);

/* Compatibility accessors. Prefer one snapshot when reading multiple fields. */
bool     thermostat_ctrl_get_power(void);
uint8_t  thermostat_ctrl_get_mode(void);         /* returns HomeKit mode */
float    thermostat_ctrl_get_target_temp(void);
float    thermostat_ctrl_get_current_temp(void);
uint8_t  thermostat_ctrl_get_fan_speed(void);
bool     thermostat_ctrl_get_swing(swing_axis_t axis);
bool     thermostat_ctrl_is_healthy(void);
uint32_t thermostat_ctrl_get_error_count(void);
