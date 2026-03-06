#include "hk_thermostat.h"
#include "controller/thermostat_ctrl.h"
#include "common/types.h"
#include "common/debug.h"
#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>

static const char *TAG = LOG_TAG_HK;

/* Thermostat service characteristics */
static hap_char_t *ch_current_state = NULL;
static hap_char_t *ch_target_state  = NULL;
static hap_char_t *ch_current_temp  = NULL;
static hap_char_t *ch_target_temp   = NULL;
static hap_char_t *ch_temp_units    = NULL;

/* Fan service characteristics */
static hap_char_t *ch_fan_active = NULL;
static hap_char_t *ch_fan_speed  = NULL;

/* Swing switch characteristic */
static hap_char_t *ch_swing_on = NULL;

/* Convert AC fan speed (0-7) to HomeKit rotation percentage (0-100) */
static float fan_speed_to_percent(uint8_t speed)
{
    switch (speed) {
    case AC_FAN_1: return 20.0f;
    case AC_FAN_2: return 40.0f;
    case AC_FAN_3: return 60.0f;
    case AC_FAN_4: return 80.0f;
    case AC_FAN_5: return 100.0f;
    default:       return 0.0f;   /* auto */
    }
}

/* Convert HomeKit rotation percentage (0-100) to AC fan speed */
static uint8_t percent_to_fan_speed(float pct)
{
    if (pct <= 0)       return AC_FAN_AUTO;
    else if (pct <= 20) return AC_FAN_1;
    else if (pct <= 40) return AC_FAN_2;
    else if (pct <= 60) return AC_FAN_3;
    else if (pct <= 80) return AC_FAN_4;
    else                return AC_FAN_5;
}

/*
 * Derive current heating/cooling state from power and mode.
 * HAP values: 0=OFF, 1=HEATING, 2=COOLING
 */
static int derive_current_state(void)
{
    if (!thermostat_ctrl_get_power()) {
        return 0;  /* OFF */
    }
    uint8_t mode = thermostat_ctrl_get_mode();
    if (mode == HAP_MODE_HEAT) return 1;  /* HEATING */
    if (mode == HAP_MODE_COOL) return 2;  /* COOLING */
    return 0;  /* IDLE for AUTO (simplified) */
}

/* HAP write callback: HomeKit -> Controller (non-blocking) */
static int thermostat_write(hap_write_data_t write_data[], int count,
                            void *serv_priv, void *write_priv)
{
    for (int i = 0; i < count; i++) {
        hap_write_data_t *w = &write_data[i];

        if (w->hc == ch_target_state) {
            uint8_t mode = w->val.i;
            ESP_LOGI(TAG, "HomeKit set mode: %d", mode);
            thermostat_ctrl_set_mode(mode);
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;
        }
        else if (w->hc == ch_target_temp) {
            float temp = w->val.f;
            ESP_LOGI(TAG, "HomeKit set temp: %.1f", temp);
            thermostat_ctrl_set_temperature(temp);
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;
        }
        else if (w->hc == ch_fan_active) {
            bool on = (w->val.i == 1);
            ESP_LOGI(TAG, "HomeKit set fan active: %d", on);
            thermostat_ctrl_set_power(on);
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;
        }
        else if (w->hc == ch_fan_speed) {
            float pct = w->val.f;
            uint8_t speed = percent_to_fan_speed(pct);
            ESP_LOGI(TAG, "HomeKit set fan speed: %.0f%% -> %d", pct, speed);
            thermostat_ctrl_set_fan_speed(speed);
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;
        }
        else if (w->hc == ch_swing_on) {
            bool on = w->val.u;
            ESP_LOGI(TAG, "HomeKit set swing: %d", on);
            thermostat_ctrl_set_swing(SWING_VERTICAL, on);
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;
        }
        else {
            *(w->status) = HAP_STATUS_RES_ABSENT;
        }
    }
    return HAP_SUCCESS;
}

/* HAP read callback: Controller cache -> HomeKit (instant, never blocks) */
static int thermostat_read(hap_char_t *hc, hap_status_t *status,
                           void *serv_priv, void *read_priv)
{
    hap_val_t val;

    if (hc == ch_current_temp) {
        val.f = thermostat_ctrl_get_current_temp();
        hap_char_update_val(hc, &val);
    }
    else if (hc == ch_target_temp) {
        val.f = thermostat_ctrl_get_target_temp();
        hap_char_update_val(hc, &val);
    }
    else if (hc == ch_current_state) {
        val.i = derive_current_state();
        hap_char_update_val(hc, &val);
    }
    else if (hc == ch_target_state) {
        val.i = thermostat_ctrl_get_mode();
        hap_char_update_val(hc, &val);
    }
    else if (hc == ch_fan_active) {
        val.i = thermostat_ctrl_get_power() ? 1 : 0;
        hap_char_update_val(hc, &val);
    }
    else if (hc == ch_fan_speed) {
        val.f = fan_speed_to_percent(thermostat_ctrl_get_fan_speed());
        hap_char_update_val(hc, &val);
    }
    else if (hc == ch_swing_on) {
        val.u = thermostat_ctrl_get_swing(SWING_VERTICAL);
        hap_char_update_val(hc, &val);
    }

    *status = HAP_STATUS_SUCCESS;
    return HAP_SUCCESS;
}

static int identify_routine(hap_acc_t *acc)
{
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

hap_acc_t *hk_thermostat_create(void)
{
    hap_acc_cfg_t acc_cfg = {
        .name = "DaiSpan Thermostat",
        .manufacturer = "DaiSpan",
        .model = "DS-TH1",
        .serial_num = "001",
        .fw_rev = "2.0.0",
        .hw_rev = "1.0",
        .pv = "1.1.0",
        .cid = HAP_CID_THERMOSTAT,
        .identify_routine = identify_routine,
    };
    hap_acc_t *accessory = hap_acc_create(&acc_cfg);
    if (!accessory) {
        ESP_LOGE(TAG, "Failed to create accessory");
        return NULL;
    }

    /* --- Thermostat Service --- */
    hap_serv_t *thermostat_svc = hap_serv_thermostat_create(
        0,      /* current state: OFF */
        0,      /* target state: OFF */
        21.0,   /* current temp */
        21.0,   /* target temp */
        0       /* units: Celsius */
    );

    /* Set valid values for target state: OFF(0), HEAT(1), COOL(2), AUTO(3) */
    ch_target_state = hap_serv_get_char_by_uuid(
        thermostat_svc, HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE);
    uint8_t valid_states[] = {0, 1, 2, 3};
    hap_char_add_valid_vals(ch_target_state, valid_states, sizeof(valid_states));

    /* Set temperature range: 16-30C, step 0.5 */
    ch_target_temp = hap_serv_get_char_by_uuid(
        thermostat_svc, HAP_CHAR_UUID_TARGET_TEMPERATURE);
    hap_char_float_set_constraints(ch_target_temp, 16.0, 30.0, 0.5);

    ch_current_state = hap_serv_get_char_by_uuid(
        thermostat_svc, HAP_CHAR_UUID_CURRENT_HEATING_COOLING_STATE);
    ch_current_temp = hap_serv_get_char_by_uuid(
        thermostat_svc, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    ch_temp_units = hap_serv_get_char_by_uuid(
        thermostat_svc, HAP_CHAR_UUID_TEMPERATURE_DISPLAY_UNITS);

    hap_serv_set_write_cb(thermostat_svc, thermostat_write);
    hap_serv_set_read_cb(thermostat_svc, thermostat_read);
    hap_acc_add_serv(accessory, thermostat_svc);

    /* --- Fan Service --- */
    hap_serv_t *fan_svc = hap_serv_fan_v2_create(0);  /* inactive */

    ch_fan_active = hap_serv_get_char_by_uuid(fan_svc, HAP_CHAR_UUID_ACTIVE);

    /* Add rotation speed characteristic: 0-100%, step 20 */
    ch_fan_speed = hap_char_rotation_speed_create(0);
    hap_char_float_set_constraints(ch_fan_speed, 0, 100, 20);
    hap_serv_add_char(fan_svc, ch_fan_speed);

    hap_serv_set_write_cb(fan_svc, thermostat_write);
    hap_serv_set_read_cb(fan_svc, thermostat_read);
    hap_acc_add_serv(accessory, fan_svc);

    /* --- Swing Switch Service --- */
    hap_serv_t *swing_svc = hap_serv_switch_create(false);

    hap_char_t *swing_name = hap_char_name_create("Swing");
    hap_serv_add_char(swing_svc, swing_name);

    ch_swing_on = hap_serv_get_char_by_uuid(swing_svc, HAP_CHAR_UUID_ON);

    hap_serv_set_write_cb(swing_svc, thermostat_write);
    hap_serv_set_read_cb(swing_svc, thermostat_read);
    hap_acc_add_serv(accessory, swing_svc);

    ESP_LOGI(TAG, "HomeKit accessory created: Thermostat + Fan + Swing");
    return accessory;
}

void hk_thermostat_sync(void)
{
    hap_val_t val;

    if (ch_current_temp) {
        val.f = thermostat_ctrl_get_current_temp();
        hap_char_update_val(ch_current_temp, &val);
    }

    if (ch_current_state) {
        val.i = derive_current_state();
        hap_char_update_val(ch_current_state, &val);
    }

    if (ch_target_state) {
        val.i = thermostat_ctrl_get_mode();
        hap_char_update_val(ch_target_state, &val);
    }

    if (ch_target_temp) {
        val.f = thermostat_ctrl_get_target_temp();
        hap_char_update_val(ch_target_temp, &val);
    }

    if (ch_fan_active) {
        val.i = thermostat_ctrl_get_power() ? 1 : 0;
        hap_char_update_val(ch_fan_active, &val);
    }

    if (ch_swing_on) {
        val.u = thermostat_ctrl_get_swing(SWING_VERTICAL);
        hap_char_update_val(ch_swing_on, &val);
    }
}
