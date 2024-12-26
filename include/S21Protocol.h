#pragma once

#include <Arduino.h>
#include "daikin_s21.h"

class S21Protocol {
private:
    HardwareSerial& serial;
    static constexpr uint32_t TIMEOUT_MS = 500;  // 通訊超時時間
    
    // 緩衝區
    static constexpr size_t BUFFER_SIZE = 256;
    uint8_t txBuffer[BUFFER_SIZE];
    uint8_t rxBuffer[BUFFER_SIZE];
    
    // 等待回應
    bool waitForResponse(uint8_t& response);

public:
    S21Protocol(HardwareSerial& s);
    
    // 發送命令並接收回應
    bool sendCommand(char cmd0, char cmd1, const uint8_t* payload = nullptr, size_t payloadLen = 0);
    
    // 解析回應數據
    bool parseResponse(uint8_t& cmd0, uint8_t& cmd1, uint8_t* payload, size_t& payloadLen);
}; 