#include "S21Protocol.h"
#include "Debug.h"

// 定義溫度和狀態更新的閾值
#define TEMP_UPDATE_THRESHOLD 0.5f  // 溫度更新閾值（攝氏度）
#define STATUS_UPDATE_INTERVAL 5000  // 增加狀態更新間隔到5秒

// 靜態變量用於記錄上一次的值
static float lastReportedTemp = 0.0f;
static uint8_t lastReportedPower = 255;
static uint8_t lastReportedMode = 255;
static float lastReportedTargetTemp = 0.0f;
static unsigned long lastStatusUpdateTime = 0;

S21Protocol::S21Protocol(HardwareSerial& s) : serial(s) {
    serial.begin(2400, SERIAL_8E2);
    DEBUG_INFO_PRINT("[S21] 初始化完成\n");
}

bool S21Protocol::waitForResponse(uint8_t& response) {
    unsigned long startTime = millis();
    while (millis() - startTime < TIMEOUT_MS) {
        if (serial.available()) {
            response = serial.read();
            DEBUG_VERBOSE_PRINT("[S21] 收到回應: 0x%02X\n", response);
            return true;
        }
    }
    DEBUG_ERROR_PRINT("[S21] 等待回應超時\n");
    return false;
}

bool S21Protocol::sendCommand(char cmd0, char cmd1, const uint8_t* payload, size_t payloadLen) {
    DEBUG_VERBOSE_PRINT("[S21] 發送命令: %c%c", cmd0, cmd1);
    if (payload && payloadLen > 0) {
        DEBUG_VERBOSE_PRINT(" 數據:");
        for (size_t i = 0; i < payloadLen; i++) {
            DEBUG_VERBOSE_PRINT(" %02X", payload[i]);
        }
    }
    DEBUG_VERBOSE_PRINT("\n");
    
    // 組裝數據包
    size_t index = 0;
    txBuffer[index++] = STX;
    txBuffer[index++] = cmd0;
    txBuffer[index++] = cmd1;
    
    if (payload && payloadLen > 0) {
        memcpy(&txBuffer[index], payload, payloadLen);
        index += payloadLen;
    }
    
    // 計算並添加校驗和
    uint8_t checksum = s21_checksum(txBuffer, index + 2);
    txBuffer[index++] = checksum;
    txBuffer[index++] = ETX;
    
    DEBUG_VERBOSE_PRINT("[S21] 發送數據包:");
    for (size_t i = 0; i < index; i++) {
        DEBUG_VERBOSE_PRINT(" %02X", txBuffer[i]);
    }
    DEBUG_VERBOSE_PRINT("\n");
    
    serial.write(txBuffer, index);
    return true;
}

// 添加模式文字轉換函數
const char* getModeText(uint8_t mode) {
    switch (mode) {
        case 0: return "OFF";    // 關機
        case 1: return "AUTO";   // 自動
        case 2: return "DRY";    // 除濕
        case 3: return "COOL";   // 製冷
        case 4: return "HEAT";   // 暖氣
        case 6: return "FAN";    // 送風
        default: return "UNKNOWN";
    }
}

// 添加風扇模式文字轉換函數
const char* getFanText(uint8_t fan) {
    switch (fan) {
        case '0': return "AUTO";
        case '1': return "QUIET";
        case '2': return "1";
        case '3': return "2";
        case '4': return "3";
        case '5': return "4";
        case '6': return "5";
        default: return "UNKNOWN";
    }
}

bool S21Protocol::parseResponse(uint8_t& cmd0, uint8_t& cmd1, uint8_t* payload, size_t& payloadLen) {
    uint8_t buffer[BUFFER_SIZE];
    size_t index = 0;
    unsigned long startTime = millis();
    bool foundSTX = false;
    
    while (millis() - startTime < TIMEOUT_MS && index < BUFFER_SIZE) {
        if (serial.available()) {
            uint8_t byte = serial.read();
            DEBUG_VERBOSE_PRINT("[S21] 收到字節: %02X\n", byte);
            
            if (!foundSTX) {
                if (byte == STX) {
                    buffer[index++] = byte;
                    foundSTX = true;
                    DEBUG_VERBOSE_PRINT("[S21] 找到STX\n");
                }
                continue;
            }
            
            buffer[index++] = byte;
            
            if (byte == ETX && index >= 5) {
                DEBUG_VERBOSE_PRINT("[S21] 找到ETX，數據包完整\n");
                break;
            }
        }
    }
    
    if (index < 5) {
        DEBUG_ERROR_PRINT("[S21] 錯誤：數據包長度不足\n");
        return false;
    }
    
    DEBUG_VERBOSE_PRINT("[S21] 收到數據包:");
    for (size_t i = 0; i < index; i++) {
        DEBUG_VERBOSE_PRINT(" %02X", buffer[i]);
    }
    DEBUG_VERBOSE_PRINT("\n");
    
    uint8_t receivedChecksum = buffer[index - 2];
    uint8_t calculatedChecksum = s21_checksum(buffer, index);
    
    if (receivedChecksum != calculatedChecksum) {
        DEBUG_ERROR_PRINT("[S21] 錯誤：校驗和不匹配 (收到=%02X 計算=%02X)\n", 
                         receivedChecksum, calculatedChecksum);
        return false;
    }
    
    cmd0 = buffer[1];
    cmd1 = buffer[2];
    
    // 提取payload
    payloadLen = index - 5;
    if (payload && payloadLen > 0) {
        memcpy(payload, &buffer[3], payloadLen);
        
        unsigned long currentTime = millis();
        
        // 只在收到特定命令時顯示狀態更新
        if (cmd0 == 'S' && cmd1 == 'H') {
            float temp = s21_decode_float_sensor(payload);
            if (abs(temp - lastReportedTemp) >= TEMP_UPDATE_THRESHOLD) {
                DEBUG_INFO_PRINT("[S21] 室內溫度更新: %.1f°C\n", temp);
                lastReportedTemp = temp;
            }
        }
        else if (cmd0 == 'G' && cmd1 == '1') {
            uint8_t power = payload[0] - '0';
            uint8_t mode = payload[1] - '0';
            float targetTemp = s21_decode_target_temp(payload[2]);
            uint8_t fan = payload[3];  // 風扇設定
            
            bool hasStateChange = power != lastReportedPower || 
                                mode != lastReportedMode || 
                                abs(targetTemp - lastReportedTargetTemp) >= TEMP_UPDATE_THRESHOLD;
            
            bool timeToUpdate = currentTime - lastStatusUpdateTime >= STATUS_UPDATE_INTERVAL;
            
            // 只在狀態變化或達到更新間隔時輸出
            if (hasStateChange || timeToUpdate) {
                if (hasStateChange) {
                    DEBUG_INFO_PRINT("[S21] 空調狀態變化: 電源=%d->%d 模式=%d(%s)->%d(%s) 目標溫度=%.1f->%.1f°C 風扇=%s\n",
                                   lastReportedPower, power,
                                   lastReportedMode, getModeText(lastReportedMode),
                                   mode, getModeText(mode),
                                   lastReportedTargetTemp, targetTemp,
                                   getFanText(fan));
                } else {
                    DEBUG_INFO_PRINT("[S21] 空調狀態更新: 電源=%d 模式=%d(%s) 目標溫度=%.1f°C 風扇=%s\n",
                                   power, mode, getModeText(mode), targetTemp, getFanText(fan));
                }
                
                lastReportedPower = power;
                lastReportedMode = mode;
                lastReportedTargetTemp = targetTemp;
                lastStatusUpdateTime = currentTime;
            }
        }
    }
    
    serial.write(ACK);
    DEBUG_VERBOSE_PRINT("[S21] 發送ACK\n");
    
    return true;
} 