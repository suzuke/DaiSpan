#include "protocol/S21Protocol.h"
#include "protocol/S21Utils.h"
#include "common/Debug.h"

// 常量定義 (基於 Faikin 規範調整)
static constexpr uint32_t RESPONSE_TIMEOUT_MS = 1000;   // 等待回應超時時間 (增加至1秒)
static constexpr uint32_t ACK_TIMEOUT_MS = 200;         // 等待 ACK 超時時間
static constexpr uint32_t INTER_BYTE_TIMEOUT_MS = 50;   // 字節間超時時間
static constexpr uint32_t COMMAND_DELAY_MS = 20;        // 命令間延遲
static constexpr uint32_t POST_COMMAND_DELAY_MS = 10;   // 命令後延遲
static constexpr size_t BUFFER_SIZE = 256;              // 緩衝區大小

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
    // 初始化狀態
    status.isConnected = false;
    status.hasErrors = false;
    status.lastError = S21ErrorCode::SUCCESS;
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
    
    // 第一步：嘗試最新的 FY00 命令（V3+ 版本檢測）
    uint8_t fyPayload[] = {'0', '0'};
    if (sendCommandInternal('F', 'Y', fyPayload, 2)) {
        uint8_t cmd0, cmd1;
        uint8_t response[32];
        size_t responseLen;
        
        if (parseResponse(cmd0, cmd1, response, responseLen)) {
            if (cmd0 == 'G' && cmd1 == 'Y' && responseLen >= 4) {
                // 解析版本號，格式為: "3.xx" 或 "4.xx"
                if (response[0] >= '3' && response[0] <= '9' && response[1] == '.') {
                    uint8_t major = response[0] - '0';
                    uint8_t minor = (response[2] - '0') * 10;
                    if (responseLen >= 5 && response[3] >= '0' && response[3] <= '9') {
                        minor += (response[3] - '0');
                    }
                    
                    DEBUG_INFO_PRINT("[S21] 檢測到協議版本 %d.%02d\n", major, minor);
                    
                    // 根據主版本和次版本號精確設置協議版本
                    if (major == 3) {
                        switch (minor) {
                            case 0:  protocolVersion = S21ProtocolVersion::V3_00; break;
                            case 1:  protocolVersion = S21ProtocolVersion::V3_01; break;
                            case 2:  protocolVersion = S21ProtocolVersion::V3_02; break;
                            case 10: protocolVersion = S21ProtocolVersion::V3_10; break;
                            case 11: protocolVersion = S21ProtocolVersion::V3_11; break;
                            case 12: protocolVersion = S21ProtocolVersion::V3_12; break;
                            case 20: protocolVersion = S21ProtocolVersion::V3_20; break;
                            case 21: protocolVersion = S21ProtocolVersion::V3_21; break;
                            case 22: protocolVersion = S21ProtocolVersion::V3_22; break;
                            case 30: protocolVersion = S21ProtocolVersion::V3_30; break;
                            case 40: protocolVersion = S21ProtocolVersion::V3_40; break;
                            case 50: protocolVersion = S21ProtocolVersion::V3_50; break;
                            default:
                                // 未知的 3.xx 版本，使用最接近的已知版本
                                if (minor < 5) protocolVersion = S21ProtocolVersion::V3_02;
                                else if (minor < 15) protocolVersion = S21ProtocolVersion::V3_12;
                                else if (minor < 25) protocolVersion = S21ProtocolVersion::V3_22;
                                else if (minor < 35) protocolVersion = S21ProtocolVersion::V3_30;
                                else if (minor < 45) protocolVersion = S21ProtocolVersion::V3_40;
                                else protocolVersion = S21ProtocolVersion::V3_50;
                                DEBUG_INFO_PRINT("[S21] 未知的 3.%02d 版本，使用最接近的版本\n", minor);
                                break;
                        }
                    } else if (major == 4) {
                        protocolVersion = S21ProtocolVersion::V4_00;
                        DEBUG_INFO_PRINT("[S21] 檢測到未來版本 4.xx，使用 V4.00\n");
                    } else {
                        protocolVersion = S21ProtocolVersion::V3_50;
                        DEBUG_INFO_PRINT("[S21] 未知主版本 %d，假設為最新的 V3.50\n", major);
                    }
                    return true;
                }
            }
        }
    } else {
        DEBUG_INFO_PRINT("[S21] FY 命令被拒絕（收到NAK），嘗試較低版本\n");
    }
    
    // 第二步：嘗試 FU 命令（某些 V3 變體）
    DEBUG_INFO_PRINT("[S21] 嘗試 FU 命令檢測 V3 變體...\n");
    if (sendCommandInternal('F', 'U', nullptr, 0)) {
        uint8_t cmd0, cmd1;
        uint8_t response[32];
        size_t responseLen;
        
        if (parseResponse(cmd0, cmd1, response, responseLen)) {
            if (cmd0 == 'G' && cmd1 == 'U') {
                DEBUG_INFO_PRINT("[S21] 檢測到 V3 變體（通過 FU 命令）\n");
                protocolVersion = S21ProtocolVersion::V3_00;
                return true;
            }
        }
    }
    
    // 第三步：嘗試 F8 命令（版本2檢測）
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
    
    // 第四步：嘗試基本 F1 命令（版本1檢測）
    DEBUG_INFO_PRINT("[S21] 嘗試檢測版本1...\n");
    if (sendCommandInternal('F', '1', nullptr, 0)) {
        uint8_t cmd0, cmd1;
        uint8_t response[32];
        size_t responseLen;
        
        if (parseResponse(cmd0, cmd1, response, responseLen)) {
            if (cmd0 == 'G' && cmd1 == '1') {
                DEBUG_INFO_PRINT("[S21] 檢測到協議版本1\n");
                protocolVersion = S21ProtocolVersion::V1;
                return true;
            }
        }
    }
    
    // 最後：假設為最早期版本
    DEBUG_INFO_PRINT("[S21] 所有版本檢測失敗，假設為 V0\n");
    protocolVersion = S21ProtocolVersion::V0;
    return true;
}

bool S21Protocol::detectFeatures() {
    features = S21Features();  // 重置所有功能標誌
    
    // 根據協議版本設置基本功能
    if (protocolVersion >= S21ProtocolVersion::V1) {
        features.hasCoolMode = true;
        features.hasHeatMode = true;
        features.hasTemperatureDisplay = true;
    }
    
    if (protocolVersion >= S21ProtocolVersion::V2) {
        features.hasAutoMode = true;
        features.hasDehumidify = true;
        features.hasFanMode = true;
        features.hasErrorReporting = true;
    }
    
    // 對於版本3及以上，檢測額外功能
    if (protocolVersion >= S21ProtocolVersion::V3_00) {
        // 使用 F2 命令檢測基本功能支援
        uint8_t payload[16];
        size_t payloadLen;
        uint8_t cmd0, cmd1;
        
        if (sendCommandInternal('F', '2')) {
            if (parseResponse(cmd0, cmd1, payload, payloadLen)) {
                if (cmd0 == 'G' && cmd1 == '2' && payloadLen >= 1) {
                    features.hasPowerfulMode = (payload[0] & 0x01) != 0;
                    features.hasEcoMode = (payload[0] & 0x02) != 0;
                    features.hasQuietMode = (payload[0] & 0x04) != 0;
                    features.hasComfortMode = (payload[0] & 0x08) != 0;
                    
                    if (payloadLen >= 2) {
                        features.hasSwingControl = (payload[1] & 0x01) != 0;
                        features.hasScheduleMode = (payload[1] & 0x02) != 0;
                        features.hasHumiditySensor = (payload[1] & 0x04) != 0;
                        features.hasOutdoorTempSensor = (payload[1] & 0x08) != 0;
                    }
                }
            }
        }
        
        // 使用 FK 命令檢測進階功能（某些 V3.1+ 版本）
        if (protocolVersion >= S21ProtocolVersion::V3_10) {
            if (sendCommandInternal('F', 'K')) {
                if (parseResponse(cmd0, cmd1, payload, payloadLen)) {
                    if (cmd0 == 'G' && cmd1 == 'K' && payloadLen >= 1) {
                        features.hasMultiZone = (payload[0] & 0x01) != 0;
                        features.hasWiFiModule = (payload[0] & 0x02) != 0;
                        features.hasAdvancedFilters = (payload[0] & 0x04) != 0;
                        features.hasEnergyMonitoring = (payload[0] & 0x08) != 0;
                    }
                }
            }
        }
        
        // 使用 FX 命令檢測最新功能（V3.2+ 版本）
        if (protocolVersion >= S21ProtocolVersion::V3_20) {
            uint8_t fxPayload[] = {'0', '0'};
            if (sendCommandInternal('F', 'X', fxPayload, 2)) {
                if (parseResponse(cmd0, cmd1, payload, payloadLen)) {
                    if (cmd0 == 'G' && cmd1 == 'X' && payloadLen >= 1) {
                        features.hasMaintenanceAlerts = (payload[0] & 0x01) != 0;
                        features.hasRemoteDiagnostics = (payload[0] & 0x02) != 0;
                    }
                }
            }
        }
    }
    
    DEBUG_INFO_PRINT("[S21] 協議版本：%d，功能支援狀態：\n", static_cast<int>(protocolVersion));
    DEBUG_INFO_PRINT("  基本模式:\n");
    DEBUG_INFO_PRINT("    - 制冷模式：%s\n", features.hasCoolMode ? "是" : "否");
    DEBUG_INFO_PRINT("    - 制熱模式：%s\n", features.hasHeatMode ? "是" : "否");
    DEBUG_INFO_PRINT("    - 自動模式：%s\n", features.hasAutoMode ? "是" : "否");
    DEBUG_INFO_PRINT("    - 除濕模式：%s\n", features.hasDehumidify ? "是" : "否");
    DEBUG_INFO_PRINT("    - 送風模式：%s\n", features.hasFanMode ? "是" : "否");
    DEBUG_INFO_PRINT("  增強功能:\n");
    DEBUG_INFO_PRINT("    - 強力模式：%s\n", features.hasPowerfulMode ? "是" : "否");
    DEBUG_INFO_PRINT("    - 節能模式：%s\n", features.hasEcoMode ? "是" : "否");
    DEBUG_INFO_PRINT("    - 靜音模式：%s\n", features.hasQuietMode ? "是" : "否");
    DEBUG_INFO_PRINT("    - 舒適模式：%s\n", features.hasComfortMode ? "是" : "否");
    DEBUG_INFO_PRINT("  感測器功能:\n");
    DEBUG_INFO_PRINT("    - 溫度顯示：%s\n", features.hasTemperatureDisplay ? "是" : "否");
    DEBUG_INFO_PRINT("    - 濕度感測器：%s\n", features.hasHumiditySensor ? "是" : "否");
    DEBUG_INFO_PRINT("    - 室外溫度感測器：%s\n", features.hasOutdoorTempSensor ? "是" : "否");
    DEBUG_INFO_PRINT("    - 錯誤報告：%s\n", features.hasErrorReporting ? "是" : "否");
    DEBUG_INFO_PRINT("  進階控制:\n");
    DEBUG_INFO_PRINT("    - 擺風控制：%s\n", features.hasSwingControl ? "是" : "否");
    DEBUG_INFO_PRINT("    - 定時功能：%s\n", features.hasScheduleMode ? "是" : "否");
    DEBUG_INFO_PRINT("    - 多區域控制：%s\n", features.hasMultiZone ? "是" : "否");
    DEBUG_INFO_PRINT("    - WiFi 模組：%s\n", features.hasWiFiModule ? "是" : "否");
    DEBUG_INFO_PRINT("  V3+ 擴展:\n");
    DEBUG_INFO_PRINT("    - 進階過濾器：%s\n", features.hasAdvancedFilters ? "是" : "否");
    DEBUG_INFO_PRINT("    - 能耗監控：%s\n", features.hasEnergyMonitoring ? "是" : "否");
    DEBUG_INFO_PRINT("    - 維護提醒：%s\n", features.hasMaintenanceAlerts ? "是" : "否");
    DEBUG_INFO_PRINT("    - 遠程診斷：%s\n", features.hasRemoteDiagnostics ? "是" : "否");
    
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
    
    // 發送數據前等待 (基於 Faikin 規範)
    delay(COMMAND_DELAY_MS);
    
    // 發送數據
    serial.write(txBuffer, index);
    serial.flush();
    
    // 等待確認
    bool result = waitForAck(ACK_TIMEOUT_MS);
    
    // 發送後等待
    delay(POST_COMMAND_DELAY_MS);
    
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
                setError(S21ErrorCode::COMMAND_NOT_SUPPORTED);
                DEBUG_ERROR_PRINT("[S21] 錯誤：收到 NAK\n");
                return false;
            } else {
                setError(S21ErrorCode::INVALID_RESPONSE);
                DEBUG_ERROR_PRINT("[S21] 錯誤：收到無效回應：0x%02X\n", response);
                return false;
            }
        }
        yield();  // 讓其他任務有機會運行
    }
    setError(S21ErrorCode::TIMEOUT);
    DEBUG_ERROR_PRINT("[S21] 錯誤：等待 ACK 超時\n");
    return false;
}

bool S21Protocol::sendCommand(char cmd0, char cmd1, const uint8_t* payload, size_t len) {
    if (!isInitialized) {
        setError(S21ErrorCode::PROTOCOL_ERROR);
        DEBUG_ERROR_PRINT("[S21] 錯誤：協議未初始化\n");
        return false;
    }
    
    // 檢查命令是否被支援
    if (!isCommandSupported(cmd0, cmd1)) {
        setError(S21ErrorCode::COMMAND_NOT_SUPPORTED);
        DEBUG_ERROR_PRINT("[S21] 錯誤：命令 %c%c 不被當前協議版本支援\n", cmd0, cmd1);
        return false;
    }
    
    // 驗證 payload
    if (!validatePayload(payload, len)) {
        return false;  // setError 已在 validatePayload 中調用
    }
    
    return sendCommandInternal(cmd0, cmd1, payload, len);
}

bool S21Protocol::isCommandSupported(char cmd0, char cmd1) const {
    // V0/V1 基本命令
    if (protocolVersion >= S21ProtocolVersion::V0) {
        // 基本狀態命令
        if (cmd0 == 'D' && cmd1 == '1') return true;  // 設置狀態
        if (cmd0 == 'F' && cmd1 == '1') return true;  // 查詢狀態
        
        // 基本感測器命令
        if (cmd0 == 'R' && cmd1 == 'H') return true;  // 查詢溫度
        if (cmd0 == 'R' && cmd1 == 'a') return true;  // 查詢環境溫度
        if (cmd0 == 'R' && cmd1 == 'N') return true;  // 查詢室內溫度
    }
    
    // V2 增強命令
    if (protocolVersion >= S21ProtocolVersion::V2) {
        // 增強狀態命令
        if (cmd0 == 'F' && cmd1 == '8') return true;  // 查詢增強狀態
        if (cmd0 == 'D' && cmd1 == '8') return true;  // 設置增強狀態
        
        // 更多感測器命令
        if (cmd0 == 'R' && cmd1 == 'L') return true;  // 查詢濕度
        if (cmd0 == 'R' && cmd1 == 'M') return true;  // 查詢室外溫度
        if (cmd0 == 'R' && cmd1 == 'X') return true;  // 查詢錯誤狀態
        
        // 基本控制命令
        if (cmd0 == 'D' && cmd1 == '3') return true;  // 風速控制
        if (cmd0 == 'D' && cmd1 == '5') return true;  // 擺風控制
    }
    
    // V3.0+ 命令
    if (protocolVersion >= S21ProtocolVersion::V3_00) {
        // 版本和功能查詢
        if (cmd0 == 'F' && cmd1 == 'Y') return true;  // 查詢版本
        if (cmd0 == 'F' && cmd1 == '2') return true;  // 查詢基本功能
        if (cmd0 == 'F' && cmd1 == 'U') return true;  // 查詢單元類型
        
        // 進階感測器命令
        if (cmd0 == 'R' && cmd1 == 'I') return true;  // 查詢電流
        if (cmd0 == 'R' && cmd1 == 'V') return true;  // 查詢電壓
        if (cmd0 == 'R' && cmd1 == 'W') return true;  // 查詢功率
    }
    
    // V3.1+ 進階命令
    if (protocolVersion >= S21ProtocolVersion::V3_10) {
        if (cmd0 == 'F' && cmd1 == 'K') return true;  // 查詢進階功能
        if (cmd0 == 'F' && cmd1 == 'Z') return true;  // 查詢區域設定
        
        // 進階控制命令
        if (cmd0 == 'D' && cmd1 == '6') return true;  // 定時器控制
        if (cmd0 == 'D' && cmd1 == '7') return true;  // 模式控制
        if (cmd0 == 'D' && cmd1 == '9') return true;  // 進階設定
        
        // 多區域命令
        if (cmd0 == 'D' && cmd1 == 'A') return true;  // 區域A控制
        if (cmd0 == 'D' && cmd1 == 'B') return true;  // 區域B控制
        if (cmd0 == 'F' && cmd1 == 'A') return true;  // 查詢區域A
        if (cmd0 == 'F' && cmd1 == 'B') return true;  // 查詢區域B
    }
    
    // V3.2+ 擴展命令
    if (protocolVersion >= S21ProtocolVersion::V3_20) {
        // FX 系列擴展命令 (需要副命令)
        if (cmd0 == 'F' && cmd1 == 'X') return true;  // 擴展查詢
        if (cmd0 == 'D' && cmd1 == 'X') return true;  // 擴展控制
        
        // 能耗和維護命令
        if (cmd0 == 'R' && cmd1 == 'E') return true;  // 查詢能耗
        if (cmd0 == 'R' && cmd1 == 'S') return true;  // 查詢系統狀態
        if (cmd0 == 'F' && cmd1 == 'M') return true;  // 查詢維護信息
    }
    
    // V3.3+ 未來擴展命令
    if (protocolVersion >= S21ProtocolVersion::V3_30) {
        // WiFi 和網路命令
        if (cmd0 == 'F' && cmd1 == 'W') return true;  // 查詢 WiFi 狀態
        if (cmd0 == 'D' && cmd1 == 'W') return true;  // WiFi 控制
        
        // 診斷命令
        if (cmd0 == 'F' && cmd1 == 'D') return true;  // 查詢診斷信息
        if (cmd0 == 'D' && cmd1 == 'D') return true;  // 診斷控制
    }
    
    // V4.0+ 未來命令預留
    if (protocolVersion >= S21ProtocolVersion::V4_00) {
        // 未來擴展命令
        if (cmd0 == 'F' && cmd1 == 'N') return true;  // 新功能查詢
        if (cmd0 == 'D' && cmd1 == 'N') return true;  // 新功能控制
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
    while (millis() - startTime < RESPONSE_TIMEOUT_MS) {
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
    
    // 讀取剩餘數據，使用字節間超時
    bool etxFound = false;
    unsigned long lastByteTime = millis();
    
    while (millis() - startTime < RESPONSE_TIMEOUT_MS && index < BUFFER_SIZE) {
        if (serial.available()) {
            rxBuffer[index] = serial.read();
            lastByteTime = millis();  // 更新最後接收字節的時間
            
            if (rxBuffer[index] == ETX) {
                DEBUG_VERBOSE_PRINT("[S21] 收到 ETX\n");
                etxFound = true;
                index++;
                break;
            }
            index++;
        } else {
            // 檢查字節間超時
            if (millis() - lastByteTime > INTER_BYTE_TIMEOUT_MS) {
                DEBUG_ERROR_PRINT("[S21] 錯誤：字節間超時\n");
                break;
            }
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
    
    // 記錄成功的通訊
    incrementSuccess();
    status.lastResponseTime = millis();
    status.isConnected = true;
    
    return true;
}

// 錯誤處理方法實現
void S21Protocol::setError(S21ErrorCode error) {
    status.lastError = error;
    status.hasErrors = true;
    if (error != S21ErrorCode::SUCCESS) {
        incrementError();
        DEBUG_ERROR_PRINT("[S21] 設置錯誤：%d\n", static_cast<int>(error));
    }
}

void S21Protocol::incrementSuccess() {
    status.successfulCommands++;
    // 如果錯誤計數較少且最近成功，則認為連接恢復
    if (status.communicationErrors > 0 && 
        status.successfulCommands % 5 == 0) {  // 每5次成功後檢查一次
        status.hasErrors = false;
        status.lastError = S21ErrorCode::SUCCESS;
    }
}

void S21Protocol::incrementError() {
    status.communicationErrors++;
    // 連續錯誤過多時標記為連接問題
    if (status.communicationErrors > status.successfulCommands + 10) {
        status.isConnected = false;
    }
}

void S21Protocol::clearErrors() {
    status.hasErrors = false;
    status.lastError = S21ErrorCode::SUCCESS;
    status.errorFlags = 0;
    // 保留計數器以便統計
    DEBUG_INFO_PRINT("[S21] 錯誤狀態已清除\n");
}

float S21Protocol::getSuccessRate() const {
    uint32_t total = status.successfulCommands + status.communicationErrors;
    if (total == 0) return 0.0f;
    return (float)status.successfulCommands / (float)total * 100.0f;
}

bool S21Protocol::validateResponse(const uint8_t* buffer, size_t len) {
    if (!buffer || len < 5) {
        setError(S21ErrorCode::INVALID_RESPONSE);
        return false;
    }
    
    // 檢查開始和結束標記
    if (buffer[0] != STX || buffer[len-1] != ETX) {
        setError(S21ErrorCode::PROTOCOL_ERROR);
        return false;
    }
    
    // 驗證校驗和
    uint8_t checksum = s21_checksum((uint8_t*)buffer, len);
    if (checksum != buffer[len-2]) {
        setError(S21ErrorCode::CHECKSUM_ERROR);
        return false;
    }
    
    return true;
}

bool S21Protocol::validatePayload(const uint8_t* payload, size_t len) {
    if (len > BUFFER_SIZE - 10) {  // 預留協議開銷空間
        setError(S21ErrorCode::BUFFER_OVERFLOW);
        return false;
    }
    
    // 檢查payload中是否包含特殊字符
    if (payload) {
        for (size_t i = 0; i < len; i++) {
            if (payload[i] == STX || payload[i] == ETX) {
                setError(S21ErrorCode::INVALID_PARAMETER);
                DEBUG_ERROR_PRINT("[S21] Payload包含特殊字符：0x%02X\n", payload[i]);
                return false;
            }
        }
    }
    
    return true;
} 