#include "ThermostatController.h"
#include "Debug.h"

// 添加模式文字轉換函數
const char* getControllerModeText(ThermostatMode mode) {
    switch (mode) {
        case ThermostatMode::OFF: return "OFF";
        case ThermostatMode::HEAT: return "HEAT";
        case ThermostatMode::COOL: return "COOL";
        case ThermostatMode::AUTO: return "AUTO";
        default: return "UNKNOWN";
    }
}

// 添加大金模式到 HomeKit 模式的轉換函數
ThermostatMode convertDaikinModeToHomeKit(uint8_t daikinMode) {
    switch (daikinMode) {
        case 0: return ThermostatMode::OFF;   // 大金 OFF
        case 1: return ThermostatMode::AUTO;  // 大金 AUTO
        case 3: return ThermostatMode::COOL;  // 大金 COOL
        case 4: return ThermostatMode::HEAT;  // 大金 HEAT
        case 2:  // 除濕
        case 6:  // 送風
        default: return ThermostatMode::OFF;
    }
}

// 添加 HomeKit 模式到大金模式的轉換函數
uint8_t convertHomeKitModeToDaikin(ThermostatMode homekitMode) {
    switch (homekitMode) {
        case ThermostatMode::OFF: return 0;   // 大金 OFF
        case ThermostatMode::AUTO: return 1;  // 大金 AUTO
        case ThermostatMode::COOL: return 3;  // 大金 COOL
        case ThermostatMode::HEAT: return 4;  // 大金 HEAT
        default: return 0;  // 預設使用 OFF 模式
    }
}

ThermostatController::ThermostatController(S21Protocol& s21) : 
  protocol(s21),
  currentTemperature(21.0),
  targetTemperature(21.0),
  currentMode(ThermostatMode::OFF),
  targetMode(ThermostatMode::OFF),
  lastUpdateTime(0),
  consecutiveErrors(0) {  // 初始化連續錯誤計數器
  
  DEBUG_INFO_PRINT("[Controller] 開始初始化...\n");
  
  // 直接讀取初始狀態
  bool tempSuccess = updateCurrentTemp();
  bool modeSuccess = updateCurrentMode();
  
  DEBUG_INFO_PRINT("[Controller] 初始化完成 - 溫度讀取：%s，模式讀取：%s\n",
                   tempSuccess ? "成功" : "失敗",
                   modeSuccess ? "成功" : "失敗");
  
  if (tempSuccess && modeSuccess) {
    DEBUG_INFO_PRINT("[Controller] 初始狀態 - 溫度：%.1f°C，模式：%d(%s)\n",
                     currentTemperature,
                     static_cast<int>(currentMode),
                     getControllerModeText(currentMode));
  }
}

bool ThermostatController::isValidModeTransition(ThermostatMode newMode) {
  return true;
}

bool ThermostatController::shouldUpdate() {
  unsigned long currentTime = millis();
  static unsigned long lastErrorTime = 0;
  static unsigned long consecutiveErrors = 0;
  
  // 如果有連續錯誤，逐��增加更新間隔
  if (consecutiveErrors > 5) {
    unsigned long interval = std::min(UPDATE_INTERVAL * (consecutiveErrors / 5), (unsigned long)5000);
    if (currentTime - lastUpdateTime >= interval) {
      lastUpdateTime = currentTime;
      return true;
    }
    return false;
  }
  
  // 正常更新間隔
  if (currentTime - lastUpdateTime >= UPDATE_INTERVAL) {
    lastUpdateTime = currentTime;
    return true;
  }
  return false;
}

bool ThermostatController::updateCurrentTemp() {
  uint8_t payload[4];
  size_t payloadLen;
  uint8_t cmd0, cmd1;
  static unsigned long lastErrorLogTime = 0;
  
  if (!protocol.sendCommand('R', 'H')) {
    if (millis() - lastErrorLogTime >= 5000) {  // 每5秒只記錄一次錯誤
      DEBUG_ERROR_PRINT("[Controller] 錯誤：無法讀取當前溫度（可能未連接空調）\n");
      lastErrorLogTime = millis();
    }
    return false;
  }
  
  // 等待並解析回應，直到收到正確的回應
  unsigned long startTime = millis();
  while (millis() - startTime < 1000) {  // 1秒超時
    if (protocol.parseResponse(cmd0, cmd1, payload, payloadLen)) {
      if (cmd0 == 'S' && cmd1 == 'H') {  // 檢查是否是我們期待的回應
        if (payloadLen >= 4) {  // 溫度數據應該有4個字節
          float newTemp = s21_decode_float_sensor(payload);
          if (abs(newTemp - currentTemperature) >= TEMP_UPDATE_THRESHOLD) {
            DEBUG_INFO_PRINT("[Controller] 溫度更新：%.1f°C -> %.1f°C\n", 
                           currentTemperature, newTemp);
            currentTemperature = newTemp;
          }
          consecutiveErrors = 0;  // 重置錯誤計數
          return true;
        }
      }
    }
  }
  
  consecutiveErrors++;  // 增加錯誤計數
  if (millis() - lastErrorLogTime >= 5000) {  // 每5秒只記錄一次錯誤
    DEBUG_ERROR_PRINT("[Controller] 錯誤：未收到正確的溫度回應（可能未連接空調）\n");
    lastErrorLogTime = millis();
  }
  return false;
}

bool ThermostatController::updateCurrentMode() {
  uint8_t payload[4];
  size_t payloadLen;
  uint8_t cmd0, cmd1;
  static unsigned long lastErrorLogTime = 0;
  
  if (!protocol.sendCommand('F', '1')) {
    if (millis() - lastErrorLogTime >= 5000) {  // 每5秒只記錄一次錯誤
      DEBUG_ERROR_PRINT("[Controller] 錯誤：無法發送 F1 命令（可能未連接空調）\n");
      lastErrorLogTime = millis();
    }
    return false;
  }
  
  // 等待並解析回應
  unsigned long startTime = millis();
  bool responseReceived = false;
  
  while (millis() - startTime < 500) {  // 縮短超時時間到 500ms
    if (protocol.parseResponse(cmd0, cmd1, payload, payloadLen)) {
      if (cmd0 == 'G' && cmd1 == '1' && payloadLen >= 4) {
        responseReceived = true;
        
        // 解析電源狀態和模式
        uint8_t power = payload[0] - '0';
        uint8_t daikinMode = payload[1] - '0';
        
        // 根據電源狀態決定模式
        ThermostatMode newMode;
        if (!power) {
          // 如果電源關閉，強制設為 OFF
          newMode = ThermostatMode::OFF;
        } else {
          // 如果電源開啟，使用當前目標模式
          newMode = targetMode;
          
          // 如果目標模式是 OFF，則設為 AUTO（因為電源已開啟）
          if (newMode == ThermostatMode::OFF) {
            newMode = ThermostatMode::AUTO;
          }
        }
        
        // 更新模式
        if (newMode != currentMode) {
          DEBUG_INFO_PRINT("[Controller] 模式更新：%d(%s) -> %d(%s)\n", 
                         static_cast<int>(currentMode), getControllerModeText(currentMode),
                         static_cast<int>(newMode), getControllerModeText(newMode));
          currentMode = newMode;
        }
        
        // 更新目標溫度
        float newTargetTemp = s21_decode_target_temp(payload[2]);
        if (abs(newTargetTemp - targetTemperature) >= TEMP_UPDATE_THRESHOLD) {
          DEBUG_INFO_PRINT("[Controller] 目標溫度更新：%.1f°C -> %.1f°C\n",
                         targetTemperature, newTargetTemp);
          targetTemperature = newTargetTemp;
        }
        
        consecutiveErrors = 0;  // 重置錯誤計數
        break;  // 收到正確回應後立即退出
      }
    }
    delay(10);  // 短暫延遲，避免過度佔用 CPU
  }
  
  if (!responseReceived) {
    consecutiveErrors++;  // 增加錯誤計數
    if (millis() - lastErrorLogTime >= 5000) {  // 每5秒只記錄一次錯誤
      DEBUG_ERROR_PRINT("[Controller] 錯誤：F1 命令超時，未收到回應（可能未連接空調）\n");
      lastErrorLogTime = millis();
    }
    return false;
  }
  
  return true;
}

bool ThermostatController::setTargetTemperature(float temp) {
    if (temp < MIN_TEMP || temp > MAX_TEMP) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：溫度設置超出範圍 (%.1f°C)。允許範圍：%.1f-%.1f°C\n", 
                       temp, MIN_TEMP, MAX_TEMP);
        return false;
    }
    
    // 構建D1命令的payload
    uint8_t payload[4];
    payload[0] = currentMode != ThermostatMode::OFF ? '1' : '0';  // power
    
    // 轉換 HomeKit 模式到大金模式
    payload[1] = '0' + convertHomeKitModeToDaikin(currentMode);
    
    // 編碼溫度
    payload[2] = s21_encode_target_temp(temp);  // 直接使用用戶設置的溫度
    payload[3] = AC_FAN_AUTO;  // 保持風扇設置不變
    
    if (!protocol.sendCommand('D', '1', payload, 4)) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：無法設置目標溫度\n");
        return false;
    }

    targetTemperature = temp;  // 保存用戶設置的溫度
    DEBUG_INFO_PRINT("[Controller] 目標溫度設置為 %.1f°C\n", temp);
    
    return true;
}

bool ThermostatController::setTargetMode(ThermostatMode mode) {
    if (!isValidModeTransition(mode)) {
        return false;
    }

    // 構建D1命令的payload
    uint8_t payload[4];
    payload[0] = mode != ThermostatMode::OFF ? '1' : '0';  // power
    
    // 轉換 HomeKit 模式到大金模式
    payload[1] = '0' + convertHomeKitModeToDaikin(mode);
    
    // 如果從 OFF 切換到其他模式，設置適當的目標溫度
    if (currentMode == ThermostatMode::OFF && mode != ThermostatMode::OFF) {
        switch (mode) {
            case ThermostatMode::COOL:
                targetTemperature = std::max(MIN_TEMP, currentTemperature - 1.0f);
                break;
            case ThermostatMode::HEAT:
                targetTemperature = std::min(MAX_TEMP, currentTemperature + 1.0f);
                break;
            case ThermostatMode::AUTO:
                targetTemperature = currentTemperature;  // AUTO 模式保持當前溫度
                break;
        }
    }
    
    // 使用目標溫度
    payload[2] = s21_encode_target_temp(targetTemperature);
    payload[3] = AC_FAN_AUTO;  // 保持風扇設置不變
    
    if (!protocol.sendCommand('D', '1', payload, 4)) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：無法設置目標模式\n");
        return false;
    }

    targetMode = mode;
    currentMode = mode;  // 立即更新當前模式，避免重複發送命令
    
    DEBUG_INFO_PRINT("[Controller] 目標模式設置為：%d(%s)，目標溫度：%.1f°C\n", 
                     static_cast<int>(mode), getControllerModeText(mode),
                     targetTemperature);
    return true;
}

float ThermostatController::getCurrentTemperature() const {
  return currentTemperature;
}

float ThermostatController::getTargetTemperature() const {
  return targetTemperature;
}

ThermostatMode ThermostatController::getCurrentMode() const {
  return currentMode;
}

ThermostatMode ThermostatController::getTargetMode() const {
  return targetMode;
}

void ThermostatController::loop() {
  static unsigned long lastLoopTime = 0;
  unsigned long currentTime = millis();
  
  // 每5秒輸出一次心跳信息，如果有連續錯誤則顯示警告
  if (currentTime - lastLoopTime >= 5000) {
    if (consecutiveErrors > 5) {
      DEBUG_INFO_PRINT("[Controller] 運行中... 警告：可能未連接空調（%lu 次連續錯誤）\n", 
                       consecutiveErrors);
    } else {
      DEBUG_INFO_PRINT("[Controller] 運行中... 距離上次更新：%lu ms\n", 
                       currentTime - lastUpdateTime);
    }
    lastLoopTime = currentTime;
  }
  
  if (shouldUpdate()) {
    DEBUG_VERBOSE_PRINT("[Controller] 開始執行狀態更新...\n");
    lastUpdateTime = currentTime;  // 更新時間戳
    
    // 獨立執行每個更新，確保一個失敗不影響其他
    bool modeUpdateSuccess = updateCurrentMode();
    bool tempUpdateSuccess = updateCurrentTemp();
    
    #ifdef DEBUG_MODE
    if (!modeUpdateSuccess || !tempUpdateSuccess) {
      if (consecutiveErrors <= 5) {  // 只在初始幾次錯誤時顯示詳細信息
        DEBUG_INFO_PRINT("[Controller] 狀態更新結果 - 模式：%s，溫度：%s\n",
                       modeUpdateSuccess ? "成功" : "失敗",
                       tempUpdateSuccess ? "成功" : "失敗");
      }
    }
    #endif
    
    DEBUG_VERBOSE_PRINT("[Controller] 狀態更新完成\n");
  }
} 