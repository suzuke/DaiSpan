#include "controller/ThermostatController.h"
#include "protocol/S21Protocol.h"
#include "protocol/S21Utils.h"
#include "common/Debug.h"

// 溫度範圍常量
static constexpr float MIN_TEMP = 16.0;
static constexpr float MAX_TEMP = 30.0;
static constexpr float TEMP_STEP = 0.5;

// 更新間隔常量
static constexpr unsigned long UPDATE_INTERVAL = 5000;  // 5秒更新一次
static constexpr unsigned long ERROR_RESET_THRESHOLD = 5;  // 5次錯誤後重置

// HomeKit 模式轉換為空調模式
static uint8_t convertHomeKitToACMode(uint8_t hapMode) {
    switch (hapMode) {
        case HAP_MODE_OFF:  return AC_MODE_AUTO;  // 關機狀態用自動模式
        case HAP_MODE_HEAT: return AC_MODE_HEAT;  // 製熱
        case HAP_MODE_COOL: return AC_MODE_COOL;  // 製冷
        case HAP_MODE_AUTO: return AC_MODE_AUTO;  // 自動
        default: return AC_MODE_AUTO;
    }
}

// 空調模式轉換為 HomeKit 模式
static uint8_t convertACToHomeKitMode(uint8_t acMode, bool isPowerOn) {
    // 如果電源關閉，始終返回 OFF 模式
    if (!isPowerOn) {
        return HAP_MODE_OFF;
    }
    
    // 只有在電源開啟時才進行正常的模式轉換
    switch (acMode) {
        case AC_MODE_HEAT: return HAP_MODE_HEAT;
        case AC_MODE_COOL: return HAP_MODE_COOL;
        case AC_MODE_AUTO: return HAP_MODE_AUTO;
        case AC_MODE_DRY:  return HAP_MODE_COOL;  // 除濕模式映射為製冷
        case AC_MODE_FAN:  return HAP_MODE_AUTO;  // 送風模式映射為自動
        default: return HAP_MODE_AUTO;
    }
}

ThermostatController::ThermostatController(S21Protocol& p) : 
    protocol(p),
    power(false),
    mode(AC_MODE_AUTO),
    targetTemperature(21.0),
    currentTemperature(21.0),
    consecutiveErrors(0),
    lastUpdateTime(0) {
    
    DEBUG_INFO_PRINT("[Controller] 開始初始化...\n");
    update();  // 初始化時更新一次狀態
    DEBUG_INFO_PRINT("[Controller] 初始化完成\n");
}

bool ThermostatController::setPower(bool on) {
    uint8_t payload[4];
    payload[0] = on ? '1' : '0';  // power
    payload[1] = '0' + mode;      // 當前模式
    payload[2] = s21_encode_target_temp(targetTemperature);
    payload[3] = AC_FAN_AUTO;     // 風扇自動模式
    
    if (!protocol.sendCommand('D', '1', payload, 4)) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：無法設置電源狀態\n");
        return false;
    }
    
    power = on;
    DEBUG_INFO_PRINT("[Controller] 電源狀態設置為：%s\n", on ? "開啟" : "關閉");
    return true;
}

bool ThermostatController::setTargetMode(uint8_t newMode) {
    // 將 HomeKit 模式轉換為空調模式
    uint8_t acMode = convertHomeKitToACMode(newMode);
    
    // 如果是切換到關機模式，直接關閉電源
    if (newMode == HAP_MODE_OFF) {
        return setPower(false);
    }
    
    // 如果當前是關機狀態且不是切換到關機模式，需要先開機
    if (!power && newMode != HAP_MODE_OFF) {
        if (!setPower(true)) {
            return false;
        }
    }
    
    uint8_t payload[4];
    payload[0] = power ? '1' : '0';  // 保持當前電源狀態
    payload[1] = '0' + acMode;       // 新模式
    payload[2] = s21_encode_target_temp(targetTemperature);
    payload[3] = AC_FAN_AUTO;        // 風扇自動模式
    
    if (!protocol.sendCommand('D', '1', payload, 4)) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：無法設置目標模式\n");
        return false;
    }
    
    mode = acMode;
    DEBUG_INFO_PRINT("[Controller] 模式設置為：%d\n", newMode);
    return true;
}

bool ThermostatController::setTargetTemperature(float temperature) {
    // 檢查溫度是否在有效範圍內
    if (temperature < MIN_TEMP || temperature > MAX_TEMP || isnan(temperature)) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：無效的溫度值 %.1f°C\n", temperature);
        return false;
    }
    
    uint8_t payload[4];
    payload[0] = power ? '1' : '0';  // 保持當前電源狀態
    payload[1] = '0' + mode;         // 保持當前模式
    payload[2] = s21_encode_target_temp(temperature);
    payload[3] = AC_FAN_AUTO;        // 風扇自動模式
    
    if (!protocol.sendCommand('D', '1', payload, 4)) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：無法設置目標溫度\n");
        return false;
    }
    
    targetTemperature = temperature;
    DEBUG_INFO_PRINT("[Controller] 目標溫度設置為：%.1f°C\n", temperature);
    return true;
}

void ThermostatController::update() {
    unsigned long currentTime = millis();
    // 檢查是否達到更新間隔
    if (currentTime - lastUpdateTime < UPDATE_INTERVAL) {
        return;
    }
    lastUpdateTime = currentTime;

    uint8_t payload[4];
    size_t payloadLen;
    uint8_t cmd0, cmd1;
    
    // 更新狀態
    if (protocol.sendCommand('F', '1')) {
        if (protocol.parseResponse(cmd0, cmd1, payload, payloadLen)) {
            if (cmd0 == 'G' && cmd1 == '1' && payloadLen >= 4) {
                power = payload[0] == '1';
                mode = payload[1] - '0';
                targetTemperature = s21_decode_target_temp(payload[2]);
                
                DEBUG_INFO_PRINT("[Controller] 狀態更新 - 電源：%s，模式：%d，目標溫度：%.1f°C\n",
                               power ? "開啟" : "關閉", mode, targetTemperature);
            }
        }
    }
    
    // 更新當前溫度
    if (protocol.sendCommand('R', 'H')) {
        if (protocol.parseResponse(cmd0, cmd1, payload, payloadLen)) {
            if (cmd0 == 'S' && cmd1 == 'H' && payloadLen >= 4) {
                float newTemp = s21_decode_float_sensor(payload);
                if (abs(newTemp - currentTemperature) >= 0.5) {
                    currentTemperature = newTemp;
                    DEBUG_INFO_PRINT("[Controller] 當前溫度更新為：%.1f°C\n", currentTemperature);
                }
            }
        }
    }
} 