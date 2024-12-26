#include "S21Controller.h"

S21Controller::S21Controller(S21Protocol& p) 
    : protocol(p), currentTemp(0), targetTemp(25), currentMode(ThermostatMode::OFF) {
}

bool S21Controller::begin() {
    // 初始化序列
    if (!protocol.sendCommand('I', 'N')) {
        return false;
    }
    
    // 更新初始狀態
    return updateCurrentTemp() && updateCurrentMode();
}

bool S21Controller::update() {
    return updateCurrentTemp() && updateCurrentMode();
}

bool S21Controller::updateCurrentTemp() {
    uint8_t payload[4];
    size_t payloadLen;
    uint8_t cmd0, cmd1;
    
    if (!protocol.sendCommand('T', 'R')) {
        return false;
    }
    
    if (!protocol.parseResponse(cmd0, cmd1, payload, payloadLen)) {
        return false;
    }
    
    if (payloadLen >= 2) {
        // 假設溫度數據在前兩個字節
        currentTemp = (payload[0] << 8 | payload[1]) / 10.0f;
        return true;
    }
    
    return false;
}

bool S21Controller::updateCurrentMode() {
    uint8_t payload[4];
    size_t payloadLen;
    uint8_t cmd0, cmd1;
    
    if (!protocol.sendCommand('M', 'R')) {
        return false;
    }
    
    if (!protocol.parseResponse(cmd0, cmd1, payload, payloadLen)) {
        return false;
    }
    
    if (payloadLen >= 1) {
        // 根據協議轉換模式
        switch (payload[0]) {
            case 0x00:
                currentMode = ThermostatMode::OFF;
                break;
            case 0x01:
                currentMode = ThermostatMode::COOL;
                break;
            case 0x02:
                currentMode = ThermostatMode::HEAT;
                break;
            case 0x03:
                currentMode = ThermostatMode::AUTO;
                break;
            default:
                currentMode = ThermostatMode::OFF;
                break;
        }
        return true;
    }
    
    return false;
}

bool S21Controller::setTargetTemp(float temp) {
    // 將溫度轉換為兩個字節
    uint16_t tempInt = static_cast<uint16_t>(temp * 10);
    uint8_t payload[2] = {
        static_cast<uint8_t>(tempInt >> 8),
        static_cast<uint8_t>(tempInt & 0xFF)
    };
    
    if (protocol.sendCommand('T', 'S', payload, 2)) {
        targetTemp = temp;
        return true;
    }
    
    return false;
}

bool S21Controller::setTargetMode(ThermostatMode mode) {
    uint8_t modeValue;
    
    // 將模式轉換為協議值
    switch (mode) {
        case ThermostatMode::OFF:
            modeValue = 0x00;
            break;
        case ThermostatMode::COOL:
            modeValue = 0x01;
            break;
        case ThermostatMode::HEAT:
            modeValue = 0x02;
            break;
        case ThermostatMode::AUTO:
            modeValue = 0x03;
            break;
        default:
            return false;
    }
    
    uint8_t payload[1] = { modeValue };
    
    if (protocol.sendCommand('M', 'S', payload, 1)) {
        currentMode = mode;
        return true;
    }
    
    return false;
} 