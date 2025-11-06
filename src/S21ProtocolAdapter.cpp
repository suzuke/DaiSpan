#include "protocol/S21ProtocolAdapter.h"
#include <algorithm>
#include <cmath>

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
    : s21Protocol(std::move(protocol)),
      lastOperationSuccess(false),
      swingCapabilityVertical(false),
      swingCapabilityHorizontal(false) {
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
        refreshSwingCapabilities();
        refreshSwingStateFromStatus(0);
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
    payload[3] = convertFanSpeedToAC(fanSpeed);  // 轉換數值到協議字符
    
    DEBUG_INFO_PRINT("[S21Adapter] 風速轉換: 數值=%d (%s) -> 協議字符='%c'\n",
                      fanSpeed, getFanSpeedText(fanSpeed), payload[3]);
    
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
    
    // S21協議溫度精度修正：四捨五入到最接近的0.5°C
    float correctedTemp = round(temperature * 2.0f) / 2.0f;
    if (fabs(correctedTemp - temperature) > 0.01f) {
        DEBUG_INFO_PRINT("[S21Adapter] 溫度精度修正：%.1f°C -> %.1f°C\n", temperature, correctedTemp);
        temperature = correctedTemp;
    }
    
    if (!validateTemperature(temperature)) {
        DEBUG_ERROR_PRINT("[S21Adapter] 溫度值超出範圍：%.1f°C (有效範圍: %.1f-%.1f°C)\n", 
                          temperature, MIN_TEMPERATURE, MAX_TEMPERATURE);
        setLastError("溫度值超出範圍");
        return false;
    }
    
    // 檢查空調電源狀態，如果狀態不確定則查詢一次
    DEBUG_INFO_PRINT("[S21Adapter] 溫度設置檢查：目標溫度=%.1f°C，當前電源狀態=%s\n", 
                      temperature, lastStatus.power ? "開啟" : "關閉");
    
    // 如果最近有電源變更，重新查詢狀態確保準確性
    if (!lastStatus.isValid) {
        DEBUG_INFO_PRINT("[S21Adapter] 狀態無效，重新查詢空調狀態\n");
        ACStatus currentStatus;
        if (queryStatus(currentStatus)) {
            lastStatus = currentStatus;
        }
    }
    
    // 如果空調關機，記錄溫度設置但不發送命令（HomeKit用戶體驗優化）
    if (!lastStatus.power) {
        DEBUG_INFO_PRINT("[S21Adapter] 空調關機時記錄目標溫度：%.1f°C（不發送S21命令）\n", temperature);
        // 更新內部狀態以支持 HomeKit 用戶體驗
        lastStatus.targetTemperature = temperature;
        lastOperationSuccess = true;
        setLastError("");
        return true; // 返回成功，讓用戶感覺設置已生效
    }
    
    // 保持當前的電源和模式狀態，只更新溫度
    uint8_t payload[4];
    payload[0] = lastStatus.power ? '1' : '0';
    payload[1] = '0' + lastStatus.mode;
    payload[2] = s21_encode_target_temp(temperature);
    payload[3] = convertFanSpeedToAC(lastStatus.fanSpeed);  // 修復：轉換風速到協議字符
    
    DEBUG_INFO_PRINT("[S21Adapter] S21命令組裝：電源=%c, 模式=%c, 溫度編碼=0x%02X('%c'), 風速=0x%02X('%c')\n",
                      payload[0], payload[1], payload[2], payload[2], payload[3], payload[3]);
    
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
    
    // 正確轉換風速字符到數值
    status.fanSpeed = convertACToFanSpeed(payload[3]);
    status.isValid = true;
    status.swingVertical = lastStatus.swingVertical;
    status.swingHorizontal = lastStatus.swingHorizontal;
    status.hasSwingVertical = swingCapabilityVertical;
    status.hasSwingHorizontal = swingCapabilityHorizontal;
    status.hasVerticalAngle = false;
    status.hasHorizontalAngle = false;
    status.verticalAngle = -1;
    status.horizontalAngle = -1;
    
    DEBUG_INFO_PRINT("[S21Adapter] 風速解析: 原始字符='%c' -> 數值=%d (%s)\n",
                      payload[3], status.fanSpeed, getFanSpeedText(status.fanSpeed));
    
    if (swingCapabilityVertical || swingCapabilityHorizontal) {
        if (s21Protocol->sendCommand('F', '5')) {
            uint8_t swingPayload[4];
            size_t swingLen;
            if (s21Protocol->parseResponse(cmd0, cmd1, swingPayload, swingLen) &&
                cmd0 == 'G' && cmd1 == '5' && swingLen >= 1) {
                refreshSwingStateFromStatus(swingPayload[0]);
                status.swingVertical = lastStatus.swingVertical;
                status.swingHorizontal = lastStatus.swingHorizontal;
                status.hasSwingVertical = lastStatus.hasSwingVertical;
                status.hasSwingHorizontal = lastStatus.hasSwingHorizontal;
            }
        }
    }

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
    // 將數値風速轉換為協議字符
    char acFanSpeed = convertFanSpeedToAC(fanSpeed);
    
    // 檢查轉換後的字符是否在支持列表中
    auto it = std::find(SUPPORTED_FAN_SPEEDS.begin(), SUPPORTED_FAN_SPEEDS.end(), acFanSpeed);
    return it != SUPPORTED_FAN_SPEEDS.end();
}

bool S21ProtocolAdapter::supportsSwing(SwingAxis axis) const {
    if (!s21Protocol) {
        return false;
    }
    if (!s21Protocol->isCommandSupported('D', '5')) {
        return false;
    }
    return axis == SwingAxis::Vertical ? swingCapabilityVertical : swingCapabilityHorizontal;
}

bool S21ProtocolAdapter::setSwing(SwingAxis axis, bool enabled) {
    if (!s21Protocol) {
        setLastError("協議未初始化");
        return false;
    }
    
    if (!supportsSwing(axis)) {
        setLastError(axis == SwingAxis::Vertical ? "不支援垂直擺風" : "不支援水平擺風");
        lastOperationSuccess = false;
        return false;
    }
    
    bool targetVertical = axis == SwingAxis::Vertical ? enabled : lastStatus.swingVertical;
    bool targetHorizontal = axis == SwingAxis::Horizontal ? enabled : lastStatus.swingHorizontal;
    
    if (!sendSwingCommand(targetVertical, targetHorizontal)) {
        setLastError("擺風命令發送失敗");
        lastOperationSuccess = false;
        return false;
    }
    
    // 嘗試立即讀回當前狀態更新緩存
    if (s21Protocol->sendCommand('F', '5')) {
        uint8_t swingPayload[4];
        size_t swingLen;
        uint8_t cmd0, cmd1;
        if (s21Protocol->parseResponse(cmd0, cmd1, swingPayload, swingLen) &&
            cmd0 == 'G' && cmd1 == '5' && swingLen >= 1) {
            refreshSwingStateFromStatus(swingPayload[0]);
        }
    } else {
        uint8_t combination = 0;
        if (targetVertical) combination |= 0x01;
        if (targetHorizontal) combination |= 0x02;
        if (targetVertical && targetHorizontal) combination |= 0x04;
        refreshSwingStateFromStatus('0' + combination);
    }
    
    lastOperationSuccess = true;
    setLastError("");
    return true;
}

bool S21ProtocolAdapter::getSwingState(SwingAxis axis, bool& enabled) const {
    if (axis == SwingAxis::Vertical) {
        if (!swingCapabilityVertical) {
            return false;
        }
        enabled = lastStatus.swingVertical;
        return true;
    }
    if (!swingCapabilityHorizontal) {
        return false;
    }
    enabled = lastStatus.swingHorizontal;
    return true;
}

bool S21ProtocolAdapter::supportsSwingAngle(SwingAxis) const {
    return false;
}

std::vector<int> S21ProtocolAdapter::getAvailableSwingAngles(SwingAxis) const {
    return {};
}

bool S21ProtocolAdapter::setSwingAngle(SwingAxis, int) {
    setLastError("S21 協議未支援擺風角度設定");
    lastOperationSuccess = false;
    return false;
}

bool S21ProtocolAdapter::getSwingAngle(SwingAxis, int& angleCode) const {
    angleCode = -1;
    return false;
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

bool S21ProtocolAdapter::sendSwingCommand(bool verticalOn, bool horizontalOn) {
    if (!s21Protocol) {
        return false;
    }
    uint8_t payload[4];
    uint8_t combination = 0;
    if (verticalOn) combination |= 0x01;
    if (horizontalOn) combination |= 0x02;
    if (verticalOn && horizontalOn) combination |= 0x04;
    
    payload[0] = static_cast<uint8_t>('0' + (combination & 0x0F));
    payload[1] = (verticalOn || horizontalOn) ? '?' : '0';
    payload[2] = '0';
    payload[3] = '0';
    
    return s21Protocol->sendCommand('D', '5', payload, 4);
}

void S21ProtocolAdapter::refreshSwingCapabilities() {
    swingCapabilityVertical = false;
    swingCapabilityHorizontal = false;
    if (!s21Protocol) {
        return;
    }
    if (!s21Protocol->isCommandSupported('D', '5')) {
        return;
    }
    
    const auto& features = s21Protocol->getFeatures();
    swingCapabilityVertical = features.hasSwingControl || features.hasVerticalSwing || !features.hasHorizontalSwing;
    swingCapabilityHorizontal = features.hasHorizontalSwing;
    
    if (!swingCapabilityVertical) {
        // 大多數設備至少支援垂直擺風；若協議標誌缺失則保守預設為支援
        swingCapabilityVertical = true;
    }
}

void S21ProtocolAdapter::refreshSwingStateFromStatus(uint8_t rawValue) {
    uint8_t combination = rawValue;
    if (rawValue >= '0' && rawValue <= '9') {
        combination = static_cast<uint8_t>(rawValue - '0');
    }
    lastStatus.swingVertical = (combination & 0x01) != 0;
    lastStatus.swingHorizontal = (combination & 0x02) != 0;
    lastStatus.hasSwingVertical = swingCapabilityVertical;
    lastStatus.hasSwingHorizontal = swingCapabilityHorizontal;
    lastStatus.hasVerticalAngle = false;
    lastStatus.hasHorizontalAngle = false;
    lastStatus.verticalAngle = -1;
    lastStatus.horizontalAngle = -1;
    lastStatus.isValid = true;
}
