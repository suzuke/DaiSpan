#include "protocol/S21ProtocolAdapter.h"
#include <algorithm>

// 支持的模式和風扇速度定義
const std::vector<uint8_t> S21ProtocolAdapter::SUPPORTED_MODES = {
    AC_MODE_AUTO,    // 自動模式
    AC_MODE_COOL,    // 制冷模式
    AC_MODE_HEAT,    // 制熱模式
    AC_MODE_DRY,     // 除濕模式（如果支持）
    AC_MODE_FAN      // 送風模式（如果支持）
};

const std::vector<uint8_t> S21ProtocolAdapter::SUPPORTED_FAN_SPEEDS = {
    AC_FAN_AUTO,     // 自動風速
    AC_FAN_QUIET,    // 靜音
    AC_FAN_1,        // 1檔
    AC_FAN_2,        // 2檔
    AC_FAN_3,        // 3檔
    AC_FAN_4,        // 4檔
    AC_FAN_5         // 5檔
};

S21ProtocolAdapter::S21ProtocolAdapter(std::unique_ptr<S21Protocol> protocol) 
    : s21Protocol(std::move(protocol)), lastOperationSuccess(false) {
    DEBUG_INFO_PRINT("[S21Adapter] S21協議適配器初始化\n");
    lastStatus.isValid = false;
}

bool S21ProtocolAdapter::begin() {
    DEBUG_INFO_PRINT("[S21Adapter] 開始初始化S21協議適配器\n");
    
    if (!s21Protocol) {
        DEBUG_ERROR_PRINT("[S21Adapter] 錯誤：S21協議實例為空\n");
        return false;
    }
    
    bool success = s21Protocol->begin();
    if (success) {
        lastOperationSuccess = true;
        setLastError("");
        DEBUG_INFO_PRINT("[S21Adapter] S21協議適配器初始化成功\n");
    } else {
        lastOperationSuccess = false;
        setLastError("S21協議初始化失敗");
        DEBUG_ERROR_PRINT("[S21Adapter] S21協議適配器初始化失敗\n");
    }
    
    return success;
}

bool S21ProtocolAdapter::setPowerAndMode(bool power, uint8_t mode, float temperature, uint8_t fanSpeed) {
    DEBUG_INFO_PRINT("[S21Adapter] 設置電源=%s, 模式=%d, 溫度=%.1f°C, 風速=%d\n", 
                      power ? "開啟" : "關閉", mode, temperature, fanSpeed);
    
    // 參數驗證
    if (!validateTemperature(temperature)) {
        setLastError("溫度值超出範圍");
        return false;
    }
    
    if (!validateMode(mode)) {
        setLastError("不支持的模式");
        return false;
    }
    
    if (!validateFanSpeed(fanSpeed)) {
        setLastError("不支持的風速");
        return false;
    }
    
    // 構建S21協議載荷
    uint8_t payload[4];
    payload[0] = power ? '1' : '0';
    payload[1] = '0' + mode;
    payload[2] = s21_encode_target_temp(temperature);
    payload[3] = fanSpeed;
    
    // 發送S21命令
    bool success = s21Protocol->sendCommand('D', '1', payload, 4);
    
    if (success) {
        lastOperationSuccess = true;
        setLastError("");
        
        // 更新內部狀態
        lastStatus.power = power;
        lastStatus.mode = mode;
        lastStatus.targetTemperature = temperature;
        lastStatus.fanSpeed = fanSpeed;
        lastStatus.isValid = true;
        
        DEBUG_INFO_PRINT("[S21Adapter] 電源和模式設置成功\n");
    } else {
        lastOperationSuccess = false;
        setLastError("S21命令發送失敗");
        DEBUG_ERROR_PRINT("[S21Adapter] 電源和模式設置失敗\n");
    }
    
    return success;
}

bool S21ProtocolAdapter::setTemperature(float temperature) {
    DEBUG_INFO_PRINT("[S21Adapter] 設置溫度=%.1f°C\n", temperature);
    
    if (!validateTemperature(temperature)) {
        setLastError("溫度值超出範圍");
        return false;
    }
    
    // 保持當前的電源和模式狀態，只更新溫度
    uint8_t payload[4];
    payload[0] = lastStatus.power ? '1' : '0';
    payload[1] = '0' + lastStatus.mode;
    payload[2] = s21_encode_target_temp(temperature);
    payload[3] = lastStatus.fanSpeed;
    
    bool success = s21Protocol->sendCommand('D', '1', payload, 4);
    
    if (success) {
        lastOperationSuccess = true;
        setLastError("");
        lastStatus.targetTemperature = temperature;
        DEBUG_INFO_PRINT("[S21Adapter] 溫度設置成功\n");
    } else {
        lastOperationSuccess = false;
        setLastError("S21溫度命令發送失敗");
        DEBUG_ERROR_PRINT("[S21Adapter] 溫度設置失敗\n");
    }
    
    return success;
}

bool S21ProtocolAdapter::queryStatus(ACStatus& status) {
    DEBUG_VERBOSE_PRINT("[S21Adapter] 查詢設備狀態\n");
    
    uint8_t payload[4];
    size_t payloadLen;
    uint8_t cmd0, cmd1;
    
    // 發送狀態查詢命令
    if (!s21Protocol->sendCommand('F', '1')) {
        setLastError("狀態查詢命令發送失敗");
        return false;
    }
    
    // 解析回應
    if (!s21Protocol->parseResponse(cmd0, cmd1, payload, payloadLen)) {
        setLastError("狀態回應解析失敗");
        return false;
    }
    
    // 驗證回應格式
    if (cmd0 != 'G' || cmd1 != '1' || payloadLen < 4) {
        setLastError("狀態回應格式錯誤");
        return false;
    }
    
    // 解析狀態數據
    status.power = (payload[0] == '1');
    status.mode = payload[1] - '0';
    status.targetTemperature = s21_decode_target_temp(payload[2]);
    status.fanSpeed = payload[3];
    status.isValid = true;
    
    // 更新內部緩存
    lastStatus = status;
    lastOperationSuccess = true;
    setLastError("");
    
    DEBUG_INFO_PRINT("[S21Adapter] 狀態查詢成功: 電源=%s, 模式=%d, 目標溫度=%.1f°C\n",
                      status.power ? "開啟" : "關閉", status.mode, status.targetTemperature);
    
    return true;
}

bool S21ProtocolAdapter::queryTemperature(float& temperature) {
    DEBUG_VERBOSE_PRINT("[S21Adapter] 查詢當前溫度\n");
    
    uint8_t payload[4];
    size_t payloadLen;
    uint8_t cmd0, cmd1;
    
    // 發送溫度查詢命令
    if (!s21Protocol->sendCommand('R', 'H')) {
        setLastError("溫度查詢命令發送失敗");
        return false;
    }
    
    // 解析回應
    if (!s21Protocol->parseResponse(cmd0, cmd1, payload, payloadLen)) {
        setLastError("溫度回應解析失敗");
        return false;
    }
    
    // 驗證回應格式
    if (cmd0 != 'S' || cmd1 != 'H' || payloadLen < 4) {
        setLastError("溫度回應格式錯誤");
        return false;
    }
    
    // 驗證數據格式
    for (int i = 0; i < 3; i++) {
        if (payload[i] < '0' || payload[i] > '9') {
            setLastError("溫度數據格式無效");
            return false;
        }
    }
    if (payload[3] != '-' && payload[3] != '+') {
        setLastError("溫度符號無效");
        return false;
    }
    
    // 解析溫度數據
    int rawTemp = s21_decode_int_sensor(payload);
    temperature = (float)rawTemp * 0.1f;
    
    // 範圍檢查
    if (temperature < -50.0f || temperature > 100.0f) {
        setLastError("溫度值超出合理範圍");
        return false;
    }
    
    // 更新內部狀態
    lastStatus.currentTemperature = temperature;
    lastOperationSuccess = true;
    setLastError("");
    
    DEBUG_VERBOSE_PRINT("[S21Adapter] 溫度查詢成功: %.1f°C\n", temperature);
    
    return true;
}

bool S21ProtocolAdapter::supportsMode(uint8_t mode) const {
    auto it = std::find(SUPPORTED_MODES.begin(), SUPPORTED_MODES.end(), mode);
    return it != SUPPORTED_MODES.end();
}

bool S21ProtocolAdapter::supportsFanSpeed(uint8_t fanSpeed) const {
    auto it = std::find(SUPPORTED_FAN_SPEEDS.begin(), SUPPORTED_FAN_SPEEDS.end(), fanSpeed);
    return it != SUPPORTED_FAN_SPEEDS.end();
}

std::pair<float, float> S21ProtocolAdapter::getTemperatureRange() const {
    return std::make_pair(MIN_TEMPERATURE, MAX_TEMPERATURE);
}

std::vector<uint8_t> S21ProtocolAdapter::getSupportedModes() const {
    return SUPPORTED_MODES;
}

std::vector<uint8_t> S21ProtocolAdapter::getSupportedFanSpeeds() const {
    return SUPPORTED_FAN_SPEEDS;
}

const char* S21ProtocolAdapter::getProtocolName() const {
    return "S21 Daikin Protocol";
}

const char* S21ProtocolAdapter::getProtocolVersion() const {
    if (!s21Protocol) return "Unknown";
    
    switch (s21Protocol->getProtocolVersion()) {
        case S21ProtocolVersion::V1: return "1.0";
        case S21ProtocolVersion::V2: return "2.0";
        case S21ProtocolVersion::V3_00: return "3.00";
        case S21ProtocolVersion::V3_10: return "3.10";
        case S21ProtocolVersion::V3_20: return "3.20";
        case S21ProtocolVersion::V3_40: return "3.40";
        default: return "Unknown";
    }
}

bool S21ProtocolAdapter::isLastOperationSuccessful() const {
    return lastOperationSuccess;
}

const char* S21ProtocolAdapter::getLastError() const {
    return lastError.c_str();
}

// 私有輔助方法實現
void S21ProtocolAdapter::setLastError(const char* error) {
    lastError = error;
    lastOperationSuccess = (strlen(error) == 0);
}

bool S21ProtocolAdapter::validateTemperature(float temperature) const {
    return !isnan(temperature) && 
           temperature >= MIN_TEMPERATURE && 
           temperature <= MAX_TEMPERATURE;
}

bool S21ProtocolAdapter::validateMode(uint8_t mode) const {
    return supportsMode(mode);
}

bool S21ProtocolAdapter::validateFanSpeed(uint8_t fanSpeed) const {
    return supportsFanSpeed(fanSpeed);
}