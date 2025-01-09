#include "protocol/S21Protocol.h"
#include "protocol/S21Utils.h"
#include "common/Debug.h"

// 定義溫度和狀態更新的閾值
#define TEMP_UPDATE_THRESHOLD 0.5f  // 溫度更新閾值（攝氏度）
#define STATUS_UPDATE_INTERVAL 5000  // 增加狀態更新間隔到5秒

// 常量定義
static constexpr uint32_t TIMEOUT_MS = 500;  // 通訊超時時間
static constexpr size_t BUFFER_SIZE = 256;   // 緩衝區大小

// 靜態變量用於記錄上一次的值
static float lastReportedTemp = 0.0f;
static uint8_t lastReportedPower = 255;
static uint8_t lastReportedMode = 255;
static float lastReportedTargetTemp = 0.0f;
static unsigned long lastStatusUpdateTime = 0;

S21Protocol::S21Protocol(HardwareSerial& s) : 
    serial(s),
    protocolVersion(S21ProtocolVersion::UNKNOWN),
    isInitialized(false) {
}

bool S21Protocol::begin() {
    DEBUG_INFO_PRINT("[S21] 開始初始化...\n");
    
    // 檢測協議版本
    if (!detectProtocolVersion()) {
        DEBUG_ERROR_PRINT("[S21] 錯誤：無法檢測協議版本\n");
        return false;
    }
    
    // 檢測功能支援
    if (!detectFeatures()) {
        DEBUG_ERROR_PRINT("[S21] 錯誤：無法檢測功能支援\n");
        return false;
    }
    
    isInitialized = true;
    DEBUG_INFO_PRINT("[S21] 初始化完成，協議版本：%d\n", static_cast<int>(protocolVersion));
    return true;
}

bool S21Protocol::detectProtocolVersion() {
    DEBUG_INFO_PRINT("[S21] 開始檢測協議版本...\n");
    
    // 首先嘗試 FY00 命令（版本3及以上）
    uint8_t fyPayload[] = {'0', '0'};
    if (sendCommandInternal('F', 'Y', fyPayload, 2)) {
        uint8_t cmd0, cmd1;
        uint8_t response[32];
        size_t responseLen;
        
        if (parseResponse(cmd0, cmd1, response, responseLen)) {
            if (cmd0 == 'G' && cmd1 == 'Y' && responseLen >= 2) {
                // 解析版本號，格式為: 3.xx
                if (response[0] == '3') {
                    uint8_t minor = 0;
                    if (responseLen >= 4) {
                        minor = (response[2] - '0') * 10;
                        if (responseLen >= 5) {
                            minor += (response[3] - '0');
                        }
                    }
                    
                    DEBUG_INFO_PRINT("[S21] 檢測到協議版本 3.%d\n", minor);
                    
                    // 根據次版本號設置協議版本
                    switch (minor) {
                        case 0: 
                            protocolVersion = S21ProtocolVersion::V3_00; 
                            break;
                        case 10: 
                            protocolVersion = S21ProtocolVersion::V3_10; 
                            break;
                        case 20: 
                            protocolVersion = S21ProtocolVersion::V3_20; 
                            break;
                        case 40: 
                            protocolVersion = S21ProtocolVersion::V3_40; 
                            break;
                        default:
                            // 如果是未知的3.xx版本，使用最接近的已知版本
                            if (minor < 10) {
                                protocolVersion = S21ProtocolVersion::V3_00;
                            } else if (minor < 20) {
                                protocolVersion = S21ProtocolVersion::V3_10;
                            } else if (minor < 40) {
                                protocolVersion = S21ProtocolVersion::V3_20;
                            } else {
                                protocolVersion = S21ProtocolVersion::V3_40;
                            }
                            DEBUG_INFO_PRINT("[S21] 未知的3.xx版本，使用最接近的版本\n");
                            break;
                    }
                    return true;
                }
            }
        }
    } else {
        DEBUG_INFO_PRINT("[S21] FY 命令被拒絕（收到NAK），嘗試較低版本\n");
    }
    
    // 如果 FY00 失敗，嘗試 F8 命令（版本2）
    DEBUG_INFO_PRINT("[S21] 嘗試檢測版本2...\n");
    if (sendCommandInternal('F', '8', nullptr, 0)) {
        uint8_t cmd0, cmd1;
        uint8_t response[32];
        size_t responseLen;
        
        if (parseResponse(cmd0, cmd1, response, responseLen)) {
            if (cmd0 == 'G' && cmd1 == '8') {
                DEBUG_INFO_PRINT("[S21] 檢測到協議版本2\n");
                protocolVersion = S21ProtocolVersion::V2;
                return true;
            }
        }
    }
    
    // 如果 F8 也失敗，假設為版本1
    DEBUG_INFO_PRINT("[S21] 假設為協議版本1\n");
    protocolVersion = S21ProtocolVersion::V1;
    return true;
}

bool S21Protocol::detectFeatures() {
    features = S21Features();  // 重置所有功能標誌
    
    // 根據協議版本設置基本功能
    if (protocolVersion >= S21ProtocolVersion::V2) {
        features.hasAutoMode = true;
        features.hasDehumidify = true;
        features.hasFanMode = true;
    }
    
    // 對於版本3及以上，檢測額外功能
    if (protocolVersion >= S21ProtocolVersion::V3_00) {
        // 使用 F2 命令檢測功能支援
        uint8_t payload[4];
        size_t payloadLen;
        uint8_t cmd0, cmd1;
        
        if (sendCommandInternal('F', '2')) {
            if (waitForAck()) {
                if (parseResponse(cmd0, cmd1, payload, payloadLen)) {
                    if (cmd0 == 'G' && cmd1 == '2' && payloadLen >= 1) {
                        features.hasPowerfulMode = (payload[0] & 0x01) != 0;
                        features.hasEcoMode = (payload[0] & 0x02) != 0;
                        features.hasTemperatureDisplay = (payload[0] & 0x04) != 0;
                    }
                }
            }
        }
    }
    
    DEBUG_INFO_PRINT("[S21] 協議版本：%d，功能支援狀態：\n", static_cast<int>(protocolVersion));
    DEBUG_INFO_PRINT("  - 自動模式：%s\n", features.hasAutoMode ? "是" : "否");
    DEBUG_INFO_PRINT("  - 除濕模式：%s\n", features.hasDehumidify ? "是" : "否");
    DEBUG_INFO_PRINT("  - 送風模式：%s\n", features.hasFanMode ? "是" : "否");
    DEBUG_INFO_PRINT("  - 強力模式：%s\n", features.hasPowerfulMode ? "是" : "否");
    DEBUG_INFO_PRINT("  - 節能模式：%s\n", features.hasEcoMode ? "是" : "否");
    DEBUG_INFO_PRINT("  - 溫度顯示：%s\n", features.hasTemperatureDisplay ? "是" : "否");
    
    return true;
}

bool S21Protocol::sendCommandInternal(char cmd0, char cmd1, const uint8_t* payload, size_t len) {
    static uint8_t txBuffer[BUFFER_SIZE];
    size_t index = 0;
    
    // 檢查緩衝區大小
    if (len + 5 > BUFFER_SIZE) {  // STX + cmd0 + cmd1 + payload + checksum + ETX
        DEBUG_ERROR_PRINT("[S21] 錯誤：命令數據太長\n");
        return false;
    }
    
    // 清空接收緩衝區
    while (serial.available()) {
        serial.read();
    }
    
    // 構建命令包
    txBuffer[index++] = STX;
    txBuffer[index++] = cmd0;
    txBuffer[index++] = cmd1;
    
    // 添加payload（如果有）
    if (payload && len > 0) {
        memcpy(&txBuffer[index], payload, len);
        index += len;
    }
    
    // 計算校驗和
    txBuffer[index++] = s21_checksum(txBuffer, index + 2);  // +2 for checksum and ETX
    txBuffer[index++] = ETX;
    
    // 發送數據前等待一段時間
    delay(50);
    
    // 發送數據
    serial.write(txBuffer, index);
    serial.flush();
    
    // 等待確認
    bool result = waitForAck(200);  // 增加等待時間到 200ms
    
    // 送後等待一段時間
    delay(50);
    
    return result;
}

bool S21Protocol::waitForAck(unsigned long timeout) {
    unsigned long startTime = millis();
    while (millis() - startTime < timeout) {
        if (serial.available()) {
            uint8_t response = serial.read();
            if (response == ACK) {
                return true;
            } else if (response == NAK) {
                DEBUG_ERROR_PRINT("[S21] 錯誤：收到 NAK\n");
                return false;
            }
        }
        delay(1);
    }
    DEBUG_ERROR_PRINT("[S21] 錯誤：等待 ACK 超時\n");
    return false;
}

bool S21Protocol::sendCommand(char cmd0, char cmd1, const uint8_t* payload, size_t len) {
    if (!isInitialized) {
        DEBUG_ERROR_PRINT("[S21] 錯誤：協議未初始化\n");
        return false;
    }
    
    // 檢查命令是否被支援
    if (!isCommandSupported(cmd0, cmd1)) {
        DEBUG_ERROR_PRINT("[S21] 錯誤：命令 %c%c 不被當前協議版本支援\n", cmd0, cmd1);
        return false;
    }
    
    return sendCommandInternal(cmd0, cmd1, payload, len);
}

bool S21Protocol::isCommandSupported(char cmd0, char cmd1) const {
    // 基本命令在所有版本都支援
    if (cmd0 == 'D' && cmd1 == '1') return true;  // 設置狀態
    if (cmd0 == 'F' && cmd1 == '1') return true;  // 查詢狀態
    if (cmd0 == 'R' && cmd1 == 'H') return true;  // 查詢溫度
    
    // 版本2及以上支援的命令
    if (protocolVersion >= S21ProtocolVersion::V2) {
        if (cmd0 == 'F' && cmd1 == '8') return true;
        if (cmd0 == 'D' && cmd1 == '8') return true;
    }
    
    // 版本3及以上支援的命令
    if (protocolVersion >= S21ProtocolVersion::V3_00) {
        if (cmd0 == 'F' && cmd1 == 'Y') return true;
        if (cmd0 == 'F' && cmd1 == '2') return true;
        if (cmd0 == 'F' && cmd1 == 'K') return true;
        if (cmd0 == 'F' && cmd1 == 'U') return true;
    }
    
    return false;
}

bool S21Protocol::isFeatureSupported(const S21Features& feature) const {
    // 比較功能標誌
    return memcmp(&features, &feature, sizeof(S21Features)) == 0;
}

bool S21Protocol::parseResponse(uint8_t& cmd0, uint8_t& cmd1, uint8_t* payload, size_t& payloadLen) {
    static uint8_t rxBuffer[BUFFER_SIZE];
    size_t index = 0;
    
    // 等待開始標記
    unsigned long startTime = millis();
    while (millis() - startTime < TIMEOUT_MS) {
        if (serial.available()) {
            uint8_t byte = serial.read();
            if (byte == STX) {
                rxBuffer[index++] = byte;
                DEBUG_VERBOSE_PRINT("[S21] 收到 STX\n");
                break;
            } else {
                DEBUG_VERBOSE_PRINT("[S21] 忽略無效字節：0x%02X\n", byte);
            }
        }
        delay(1);
    }
    
    if (index == 0) {
        DEBUG_ERROR_PRINT("[S21] 錯誤：等待 STX 超時\n");
        return false;
    }
    
    // 讀取剩餘數據
    bool etxFound = false;
    while (millis() - startTime < TIMEOUT_MS && index < BUFFER_SIZE) {
        if (serial.available()) {
            rxBuffer[index] = serial.read();
            if (rxBuffer[index] == ETX) {
                DEBUG_VERBOSE_PRINT("[S21] 收到 ETX\n");
                etxFound = true;
                index++;
                break;
            }
            index++;
        }
        delay(1);
    }
    
    if (!etxFound) {
        DEBUG_ERROR_PRINT("[S21] 錯誤：等待 ETX 超時或緩衝區溢出\n");
        return false;
    }
    
    // 檢查數據最小長度
    if (index < 5) {
        DEBUG_ERROR_PRINT("[S21] 錯誤：數據包太短（%d 字節）\n", index);
        return false;
    }
    
    // 驗證校驗和
    uint8_t checksum = s21_checksum(rxBuffer, index);
    
    if (checksum != rxBuffer[index - 2]) {
        DEBUG_ERROR_PRINT("[S21] 錯誤：校驗和不匹配（計算值：0x%02X，接收值：0x%02X）\n",
                         checksum, rxBuffer[index - 2]);
        return false;
    }
    
    // 提取命令和數據
    cmd0 = rxBuffer[1];
    cmd1 = rxBuffer[2];
    payloadLen = index - 5;
    if (payloadLen > 0) {
        memcpy(payload, &rxBuffer[3], payloadLen);
    }
    
    // 特殊處理 G1 命令的回應
    if (cmd0 == 'G' && cmd1 == '1' && payloadLen >= 4) {
        bool power = payload[0] == '1';
        uint8_t rawMode = payload[1] - '0';  // 將 ASCII 字符轉換為數字
        uint8_t mappedMode = convertACToHomeKitMode(rawMode, power);
        float targetTemp = s21_decode_target_temp(payload[2]);
        uint8_t fanSpeed = s21_decode_fan(payload[3]);
        
        DEBUG_INFO_PRINT("[S21] 空調狀態更新: 電源=%d 大金模式=%d(%s) -> HomeKit模式=%d(%s) 目標溫度=%.1f°C 風扇=%s\n",
                        power, rawMode, getACModeText(rawMode), 
                        mappedMode, getHomeKitModeText(mappedMode),
                        targetTemp, getFanSpeedText(fanSpeed));
    }
    
    DEBUG_VERBOSE_PRINT("[S21] 成功解析回應：cmd=%c%c，payload長度=%d\n",
                       cmd0, cmd1, payloadLen);
    
    // 發送確認
    delay(10);
    serial.write(ACK);
    serial.flush();
    
    return true;
} 