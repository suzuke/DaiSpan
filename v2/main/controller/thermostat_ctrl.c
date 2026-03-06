#include "thermostat_ctrl.h"
#include "s21/s21_protocol.h"
#include "common/debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = LOG_TAG_CTRL;

/* Command queue: HomeKit -> Controller */
static QueueHandle_t cmd_queue = NULL;

/*
 * Cached state (atomically updated by controller task, read by HomeKit task).
 * On ESP32 single-core (C3), reads/writes of aligned 32-bit values are atomic.
 */
static volatile ac_status_t cached_status = {
    .power           = false,
    .mode            = AC_MODE_AUTO,
    .target_temp     = 21.0f,
    .current_temp    = 21.0f,
    .fan_speed       = AC_FAN_AUTO,
    .swing_vertical  = false,
    .swing_horizontal = false,
    .valid           = false,
};

/* Local mutable copy used only by the controller task */
static ac_status_t local_status;

/* Error tracking */
static volatile uint32_t consecutive_errors = 0;
static uint32_t last_successful_ms = 0;

/* User interaction protection */
static uint32_t last_mode_set_ms = 0;
static uint8_t  last_user_mode   = AC_MODE_AUTO;
static uint32_t last_fan_set_ms  = 0;
static uint8_t  last_user_fan    = AC_FAN_AUTO;

/* HomeKit target mode (preserved across lossy AC mode conversions) */
static uint8_t target_homekit_mode = HAP_MODE_AUTO;

/* Dirty flags for commands pending S21 sync */
static bool dirty_power = false;
static bool dirty_mode  = false;
static bool dirty_temp  = false;
static bool dirty_fan   = false;
static bool dirty_swing = false;

#define CMD_QUEUE_SIZE         8
#define POLL_INTERVAL_MS       6000
#define ERROR_RECOVERY_MS      30000
#define MAX_CONSECUTIVE_ERRORS 10
#define MODE_PROTECT_MS        30000
#define FAN_PROTECT_MS         10000
#define CTRL_TASK_STACK        4096
#define CTRL_TASK_PRIORITY     5

static bool is_recovering(void)
{
    return consecutive_errors >= MAX_CONSECUTIVE_ERRORS;
}

/* Publish local_status to cached_status (read by HomeKit) */
static void publish_status(void)
{
    /*
     * On single-core ESP32-C3, struct copy is safe as long as readers
     * tolerate slight inconsistency between individual fields.
     */
    cached_status = local_status;
}

static void handle_error(const char *op)
{
    consecutive_errors++;
    ESP_LOGW(TAG, "%s failed (errors: %lu)", op, (unsigned long)consecutive_errors);
    if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
        ESP_LOGE(TAG, "Entering error recovery mode");
    }
}

static void reset_errors(void)
{
    if (consecutive_errors > 0) {
        ESP_LOGI(TAG, "Errors cleared (was: %lu)", (unsigned long)consecutive_errors);
        consecutive_errors = 0;
    }
    last_successful_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/* Retry sending any commands that failed on the initial attempt */
static void sync_dirty_state(void)
{
    if (!dirty_power && !dirty_mode && !dirty_temp && !dirty_fan && !dirty_swing) {
        return;
    }

    ESP_LOGI(TAG, "Syncing dirty state (P:%d M:%d T:%d F:%d S:%d)",
             dirty_power, dirty_mode, dirty_temp, dirty_fan, dirty_swing);

    if (dirty_power || dirty_mode || dirty_fan) {
        if (s21_set_power_and_mode(local_status.power, local_status.mode,
                                   local_status.target_temp, local_status.fan_speed)) {
            dirty_power = false;
            dirty_fan   = false;
            dirty_temp  = false;  /* set_power_and_mode includes temp */
            if (dirty_mode) {
                last_mode_set_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                last_user_mode   = local_status.mode;
                dirty_mode       = false;
            }
            reset_errors();
        } else {
            handle_error("sync_dirty");
            return;
        }
    }

    if (dirty_temp) {
        if (s21_set_power_and_mode(local_status.power, local_status.mode,
                                   local_status.target_temp, local_status.fan_speed)) {
            dirty_temp = false;
            reset_errors();
        } else {
            handle_error("sync_temp");
            return;
        }
    }

    if (dirty_swing) {
        if (s21_set_swing(local_status.swing_vertical, local_status.swing_horizontal)) {
            dirty_swing = false;
            reset_errors();
        } else {
            handle_error("sync_swing");
        }
    }
}

/* Try to immediately send a D1 command; mark dirty on failure */
static bool try_send_d1(void)
{
    if (s21_set_power_and_mode(local_status.power, local_status.mode,
                               local_status.target_temp, local_status.fan_speed)) {
        dirty_power = false;
        dirty_mode  = false;
        dirty_temp  = false;
        dirty_fan   = false;
        reset_errors();
        return true;
    }
    handle_error("send_d1");
    return false;
}

static void process_command(const ctrl_cmd_t *cmd)
{
    switch (cmd->type) {
    case CMD_SET_POWER:
        ESP_LOGI(TAG, "Set power: %d", cmd->data.power);
        local_status.power = cmd->data.power;
        dirty_power = true;
        if (!is_recovering()) {
            try_send_d1();
        }
        publish_status();
        break;

    case CMD_SET_MODE: {
        uint8_t hk_mode = cmd->data.mode;
        ESP_LOGI(TAG, "Set mode: HK=%d", hk_mode);

        if (hk_mode == HAP_MODE_OFF) {
            local_status.power = false;
            dirty_power = true;
        } else {
            uint8_t ac_mode  = homekit_to_ac_mode(hk_mode);
            local_status.mode = ac_mode;
            target_homekit_mode = hk_mode;
            last_mode_set_ms    = xTaskGetTickCount() * portTICK_PERIOD_MS;
            last_user_mode      = ac_mode;
            dirty_mode = true;
            if (!local_status.power) {
                local_status.power = true;
                dirty_power = true;
            }
        }

        if (!is_recovering()) {
            try_send_d1();
        }
        publish_status();
        break;
    }

    case CMD_SET_TEMP:
        ESP_LOGI(TAG, "Set temp: %.1f", cmd->data.temperature);
        local_status.target_temp = cmd->data.temperature;
        dirty_temp = true;
        if (!is_recovering()) {
            try_send_d1();
        }
        publish_status();
        break;

    case CMD_SET_FAN:
        ESP_LOGI(TAG, "Set fan: %d", cmd->data.fan_speed);
        local_status.fan_speed = cmd->data.fan_speed;
        last_fan_set_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        last_user_fan   = cmd->data.fan_speed;
        dirty_fan = true;
        if (!is_recovering()) {
            try_send_d1();
        }
        publish_status();
        break;

    case CMD_SET_SWING: {
        swing_axis_t axis = cmd->data.swing.axis;
        bool enabled      = cmd->data.swing.enabled;
        ESP_LOGI(TAG, "Set swing: axis=%d enabled=%d", axis, enabled);
        if (axis == SWING_VERTICAL) {
            local_status.swing_vertical = enabled;
        } else {
            local_status.swing_horizontal = enabled;
        }
        dirty_swing = true;
        if (!is_recovering()) {
            if (s21_set_swing(local_status.swing_vertical,
                              local_status.swing_horizontal)) {
                dirty_swing = false;
                reset_errors();
            } else {
                handle_error("set_swing");
            }
        }
        publish_status();
        break;
    }

    default:
        break;
    }
}

static void poll_status(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool any_success = false;

    /* Query device status (F1 -> G1) */
    ac_status_t new_status = {0};
    if (s21_query_status(&new_status)) {
        any_success = true;
        local_status.power = new_status.power;

        /* Mode protection period: keep user-set mode until AC settles */
        if (last_mode_set_ms > 0 && (now - last_mode_set_ms) < MODE_PROTECT_MS) {
            local_status.mode = last_user_mode;
            ESP_LOGD(TAG, "Mode protected (%lu ms left)",
                     (unsigned long)(MODE_PROTECT_MS - (now - last_mode_set_ms)));
        } else {
            local_status.mode = new_status.mode;
            /* Update HomeKit mode only for lossless conversions */
            switch (new_status.mode) {
            case AC_MODE_HEAT:   target_homekit_mode = HAP_MODE_HEAT; break;
            case AC_MODE_COOL:   target_homekit_mode = HAP_MODE_COOL; break;
            case AC_MODE_AUTO:
            case AC_MODE_AUTO_2: target_homekit_mode = HAP_MODE_AUTO; break;
            /* DRY/FAN: preserve target_homekit_mode */
            default: break;
            }
        }

        local_status.target_temp = new_status.target_temp;

        /* Fan speed protection */
        if (last_fan_set_ms > 0 && (now - last_fan_set_ms) < FAN_PROTECT_MS) {
            local_status.fan_speed = last_user_fan;
        } else {
            local_status.fan_speed = new_status.fan_speed;
        }
    } else {
        handle_error("poll_status");
    }

    /* Query temperature (RH -> SH), only if status succeeded */
    if (any_success) {
        float temp;
        if (s21_query_temperature(&temp)) {
            local_status.current_temp = temp;
        } else {
            handle_error("poll_temp");
            any_success = false;
        }
    }

    /* Query swing (F5 -> G5), less frequently, only if status ok */
    if (any_success) {
        bool sv, sh;
        if (s21_query_swing(&sv, &sh)) {
            local_status.swing_vertical   = sv;
            local_status.swing_horizontal = sh;
        }
        /* Don't count swing query failure as error - some ACs don't support F5 */
    }

    if (any_success) {
        local_status.valid = true;
        reset_errors();
    }

    publish_status();
}

static void controller_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Controller task started");

    /* Initial delay for UART to settle, then first status poll */
    vTaskDelay(pdMS_TO_TICKS(1000));
    poll_status();

    TickType_t last_poll = xTaskGetTickCount();

    for (;;) {
        /* Process queued commands (non-blocking peek with short timeout) */
        ctrl_cmd_t cmd;
        while (xQueueReceive(cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            process_command(&cmd);
        }

        /* Periodic status polling */
        uint32_t now_ms       = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t last_poll_ms = last_poll * portTICK_PERIOD_MS;

        if (is_recovering()) {
            /* In recovery: try to recover after interval */
            if (now_ms - last_successful_ms > ERROR_RECOVERY_MS) {
                ESP_LOGI(TAG, "Attempting recovery...");
                consecutive_errors = MAX_CONSECUTIVE_ERRORS - 1;  /* one chance */
                sync_dirty_state();
                if (!is_recovering()) {
                    poll_status();
                    last_poll = xTaskGetTickCount();
                }
            }
        } else if (now_ms - last_poll_ms >= POLL_INTERVAL_MS) {
            /* Normal polling: sync dirty state first, then poll */
            sync_dirty_state();
            if (!is_recovering()) {
                poll_status();
            }
            last_poll = xTaskGetTickCount();
        }
    }
}

/* --- Public API --- */

esp_err_t thermostat_ctrl_init(void)
{
    cmd_queue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(ctrl_cmd_t));
    if (!cmd_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return ESP_ERR_NO_MEM;
    }

    memset(&local_status, 0, sizeof(local_status));
    local_status.target_temp  = 21.0f;
    local_status.current_temp = 21.0f;

    BaseType_t ret = xTaskCreate(controller_task, "ctrl_task", CTRL_TASK_STACK,
                                 NULL, CTRL_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create controller task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Controller initialized");
    return ESP_OK;
}

static bool send_cmd(const ctrl_cmd_t *cmd)
{
    if (!cmd_queue) {
        return false;
    }
    /* Non-blocking send: if queue full, drop (HomeKit must never block) */
    return xQueueSend(cmd_queue, cmd, 0) == pdTRUE;
}

bool thermostat_ctrl_set_power(bool on)
{
    ctrl_cmd_t cmd = { .type = CMD_SET_POWER, .data.power = on };
    return send_cmd(&cmd);
}

bool thermostat_ctrl_set_mode(uint8_t homekit_mode)
{
    ctrl_cmd_t cmd = { .type = CMD_SET_MODE, .data.mode = homekit_mode };
    return send_cmd(&cmd);
}

bool thermostat_ctrl_set_temperature(float temp)
{
    if (temp < 16.0f || temp > 30.0f) {
        return false;
    }
    ctrl_cmd_t cmd = { .type = CMD_SET_TEMP, .data.temperature = temp };
    return send_cmd(&cmd);
}

bool thermostat_ctrl_set_fan_speed(uint8_t speed)
{
    ctrl_cmd_t cmd = { .type = CMD_SET_FAN, .data.fan_speed = speed };
    return send_cmd(&cmd);
}

bool thermostat_ctrl_set_swing(swing_axis_t axis, bool enabled)
{
    ctrl_cmd_t cmd = {
        .type = CMD_SET_SWING,
        .data.swing = { .axis = axis, .enabled = enabled }
    };
    return send_cmd(&cmd);
}

bool thermostat_ctrl_get_power(void)
{
    return cached_status.power;
}

uint8_t thermostat_ctrl_get_mode(void)
{
    return cached_status.power ? target_homekit_mode : HAP_MODE_OFF;
}

float thermostat_ctrl_get_target_temp(void)
{
    return cached_status.target_temp;
}

float thermostat_ctrl_get_current_temp(void)
{
    return cached_status.current_temp;
}

uint8_t thermostat_ctrl_get_fan_speed(void)
{
    return cached_status.fan_speed;
}

bool thermostat_ctrl_get_swing(swing_axis_t axis)
{
    return (axis == SWING_VERTICAL)
        ? cached_status.swing_vertical
        : cached_status.swing_horizontal;
}

bool thermostat_ctrl_is_healthy(void)
{
    return consecutive_errors < MAX_CONSECUTIVE_ERRORS;
}

uint32_t thermostat_ctrl_get_error_count(void)
{
    return consecutive_errors;
}
