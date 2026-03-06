#pragma once
#include <stdint.h>
#include <stdbool.h>

/* AC modes (internal S21 protocol values) */
#define AC_MODE_AUTO   0
#define AC_MODE_DRY    1
#define AC_MODE_COOL   2
#define AC_MODE_HEAT   3
#define AC_MODE_FAN    6
#define AC_MODE_AUTO_2 7

/* HomeKit thermostat modes */
#define HAP_MODE_OFF   0
#define HAP_MODE_HEAT  1
#define HAP_MODE_COOL  2
#define HAP_MODE_AUTO  3

/* Fan speeds (S21 protocol character values) */
#define AC_FAN_AUTO    0
#define AC_FAN_QUIET   1
#define AC_FAN_1       3
#define AC_FAN_2       4
#define AC_FAN_3       5
#define AC_FAN_4       6
#define AC_FAN_5       7

/* Swing axis */
typedef enum {
    SWING_VERTICAL = 0,
    SWING_HORIZONTAL = 1
} swing_axis_t;

/* AC status reported by S21 protocol */
typedef struct {
    bool power;
    uint8_t mode;
    float target_temp;
    float current_temp;
    uint8_t fan_speed;
    bool swing_vertical;
    bool swing_horizontal;
    bool valid;
} ac_status_t;

/* Controller command types (for FreeRTOS queue) */
typedef enum {
    CMD_SET_POWER,
    CMD_SET_MODE,
    CMD_SET_TEMP,
    CMD_SET_FAN,
    CMD_SET_SWING,
    CMD_QUERY_STATUS,
    CMD_QUERY_TEMP,
} ctrl_cmd_type_t;

/* Controller command (sent from HomeKit task to S21 task) */
typedef struct {
    ctrl_cmd_type_t type;
    union {
        bool power;
        uint8_t mode;
        float temperature;
        uint8_t fan_speed;
        struct {
            swing_axis_t axis;
            bool enabled;
        } swing;
    } data;
} ctrl_cmd_t;

/* Controller result (sent from S21 task back) */
typedef struct {
    ctrl_cmd_type_t type;
    bool success;
    ac_status_t status;  /* populated for query commands */
} ctrl_result_t;

/* Mode conversion: HomeKit -> AC */
static inline uint8_t homekit_to_ac_mode(uint8_t hk_mode)
{
    switch (hk_mode) {
        case HAP_MODE_HEAT: return AC_MODE_HEAT;
        case HAP_MODE_COOL: return AC_MODE_COOL;
        case HAP_MODE_AUTO: return AC_MODE_AUTO;
        default:            return AC_MODE_AUTO;
    }
}

/* Mode conversion: AC -> HomeKit */
static inline uint8_t ac_to_homekit_mode(uint8_t ac_mode)
{
    switch (ac_mode) {
        case AC_MODE_HEAT:   return HAP_MODE_HEAT;
        case AC_MODE_COOL:   return HAP_MODE_COOL;
        case AC_MODE_AUTO:
        case AC_MODE_AUTO_2: return HAP_MODE_AUTO;
        default:             return HAP_MODE_AUTO;  /* DRY/FAN map to AUTO */
    }
}
