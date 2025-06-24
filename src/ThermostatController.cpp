#include "controller/ThermostatController.h"
#include "common/Debug.h"

ThermostatController::ThermostatController(std::unique_ptr<IACProtocol> p) 
    : protocol(std::move(p)),
      power(false),
      mode(AC_MODE_AUTO),
      targetTemperature(21.0),
      currentTemperature(21.0),
      fanSpeed(AC_FAN_AUTO),
      consecutiveErrors(0),
      lastUpdateTime(0),
      lastSuccessfulUpdate(0) {
    
    if (!protocol) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：協議實例為空\n");
        return;
    }
    
    DEBUG_INFO_PRINT("[Controller] 開始初始化通用控制器 - 協議: %s\n", 
                      protocol->getProtocolName());
    
    // 初始化協議
    if (protocol->begin()) {
        DEBUG_INFO_PRINT("[Controller] 協議初始化成功\n");
        update(); // 初始化時更新一次狀態
    } else {
        DEBUG_ERROR_PRINT("[Controller] 協議初始化失敗\n");
        consecutiveErrors = 1;
    }
    
    DEBUG_INFO_PRINT("[Controller] 初始化完成 - 協議: %s v%s\n", 
                      getProtocolName(), getProtocolVersion());
}

ThermostatController::ThermostatController(ThermostatController&& other) noexcept
    : protocol(std::move(other.protocol)),
      power(other.power),
      mode(other.mode),
      targetTemperature(other.targetTemperature),
      currentTemperature(other.currentTemperature),
      fanSpeed(other.fanSpeed),
      consecutiveErrors(other.consecutiveErrors),
      lastUpdateTime(other.lastUpdateTime),
      lastSuccessfulUpdate(other.lastSuccessfulUpdate) {
    
    DEBUG_INFO_PRINT("[Controller] 移動構造函數執行\n");
}

ThermostatController& ThermostatController::operator=(ThermostatController&& other) noexcept {
    if (this != &other) {
        protocol = std::move(other.protocol);
        power = other.power;
        mode = other.mode;
        targetTemperature = other.targetTemperature;
        currentTemperature = other.currentTemperature;
        fanSpeed = other.fanSpeed;
        consecutiveErrors = other.consecutiveErrors;
        lastUpdateTime = other.lastUpdateTime;
        lastSuccessfulUpdate = other.lastSuccessfulUpdate;
        
        DEBUG_INFO_PRINT("[Controller] 移動賦值操作符執行\n");
    }
    return *this;
}

bool ThermostatController::setPower(bool on) {
    if (!protocol) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：協議實例無效\n");
        return false;
    }
    
    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 跳過操作：處於錯誤恢復模式\n");
        return false;
    }
    
    DEBUG_INFO_PRINT("[Controller] 設置電源狀態：%s\n", on ? "開啟" : "關閉");
    
    // 使用抽象協議介面設置電源和模式
    bool success = protocol->setPowerAndMode(on, mode, targetTemperature, fanSpeed);
    
    if (success) {
        power = on;
        resetErrorCount();
        DEBUG_INFO_PRINT("[Controller] 電源狀態設置成功\n");
    } else {
        handleProtocolError("setPower");
    }
    
    return success;
}

bool ThermostatController::setTargetMode(uint8_t newMode) {
    if (!protocol) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：協議實例無效\n");
        return false;
    }
    
    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 跳過操作：處於錯誤恢復模式\n");
        return false;
    }
    
    // 將 HomeKit 模式轉換為空調模式
    uint8_t acMode = convertHomeKitToACMode(newMode);
    
    DEBUG_INFO_PRINT("[Controller] 設置目標模式：HomeKit=%d -> AC=%d\n", newMode, acMode);
    
    // 檢查協議是否支持該模式
    if (!protocol->supportsMode(acMode)) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：協議不支持模式 %d\n", acMode);
        return false;
    }
    
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
    
    // 使用抽象協議介面設置模式
    bool success = protocol->setPowerAndMode(power, acMode, targetTemperature, fanSpeed);
    
    if (success) {
        mode = acMode;
        resetErrorCount();
        DEBUG_INFO_PRINT("[Controller] 模式設置成功：%d\n", newMode);
    } else {
        handleProtocolError("setTargetMode");
    }
    
    return success;
}

bool ThermostatController::setTargetTemperature(float temperature) {
    if (!protocol) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：協議實例無效\n");
        return false;
    }
    
    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 跳過操作：處於錯誤恢復模式\n");
        return false;
    }
    
    // 檢查溫度範圍
    auto tempRange = protocol->getTemperatureRange();
    if (temperature < tempRange.first || temperature > tempRange.second || isnan(temperature)) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：無效的溫度值 %.1f°C (範圍: %.1f-%.1f°C)\n", 
                          temperature, tempRange.first, tempRange.second);
        return false;
    }
    
    DEBUG_INFO_PRINT("[Controller] 設置目標溫度：%.1f°C\n", temperature);
    
    // 使用抽象協議介面設置溫度
    bool success = protocol->setTemperature(temperature);
    
    if (success) {
        targetTemperature = temperature;
        resetErrorCount();
        DEBUG_INFO_PRINT("[Controller] 目標溫度設置成功\n");
    } else {
        handleProtocolError("setTargetTemperature");
    }
    
    return success;
}

void ThermostatController::update() {
    unsigned long currentTime = millis();
    
    if (!protocol) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：協議實例無效\n");
        return;
    }
    
    // 檢查更新間隔
    if (currentTime - lastUpdateTime < UPDATE_INTERVAL) {
        return;
    }
    
    lastUpdateTime = currentTime;
    
    // 如果處於錯誤恢復模式，檢查是否可以恢復
    if (isInErrorRecoveryMode()) {
        if (currentTime - lastSuccessfulUpdate > ERROR_RECOVERY_INTERVAL) {
            DEBUG_INFO_PRINT("[Controller] 嘗試從錯誤恢復模式恢復\n");
            consecutiveErrors = MAX_CONSECUTIVE_ERRORS - 1; // 給一次機會
        } else {
            return; // 仍在恢復期間
        }
    }
    
    DEBUG_VERBOSE_PRINT("[Controller] 開始更新狀態\n");
    
    // 查詢設備狀態
    ACStatus status;
    if (protocol->queryStatus(status)) {
        if (status.isValid) {
            power = status.power;
            mode = status.mode;
            targetTemperature = status.targetTemperature;
            fanSpeed = status.fanSpeed;
            
            DEBUG_VERBOSE_PRINT("[Controller] 狀態更新成功 - 電源：%s，模式：%d，目標溫度：%.1f°C\n",
                               power ? "開啟" : "關閉", mode, targetTemperature);
        }
        resetErrorCount();
    } else {
        handleProtocolError("queryStatus");
    }
    
    // 查詢當前溫度
    float newTemperature;
    if (protocol->queryTemperature(newTemperature)) {
        currentTemperature = newTemperature;
        DEBUG_VERBOSE_PRINT("[Controller] 溫度更新成功：%.1f°C\n", currentTemperature);
        resetErrorCount();
    } else {
        handleProtocolError("queryTemperature");
    }
    
    // 如果所有操作都成功，更新成功時間
    if (protocol->isLastOperationSuccessful()) {
        lastSuccessfulUpdate = currentTime;
    }
}

bool ThermostatController::supportsMode(uint8_t mode) const {
    return protocol ? protocol->supportsMode(mode) : false;
}

bool ThermostatController::supportsFanSpeed(uint8_t fanSpeed) const {
    return protocol ? protocol->supportsFanSpeed(fanSpeed) : false;
}

std::pair<float, float> ThermostatController::getTemperatureRange() const {
    return protocol ? protocol->getTemperatureRange() : std::make_pair(16.0f, 30.0f);
}

const char* ThermostatController::getProtocolName() const {
    return protocol ? protocol->getProtocolName() : "Unknown";
}

const char* ThermostatController::getProtocolVersion() const {
    return protocol ? protocol->getProtocolVersion() : "Unknown";
}

// 私有輔助方法實現
bool ThermostatController::handleProtocolError(const char* operation) {
    consecutiveErrors++;
    DEBUG_ERROR_PRINT("[Controller] 操作失敗：%s (連續錯誤: %lu)\n", 
                      operation, consecutiveErrors);
    
    if (protocol) {
        DEBUG_ERROR_PRINT("[Controller] 協議錯誤：%s\n", protocol->getLastError());
    }
    
    if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
        DEBUG_ERROR_PRINT("[Controller] 進入錯誤恢復模式\n");
        return false;
    }
    
    return true;
}

bool ThermostatController::isInErrorRecoveryMode() const {
    return consecutiveErrors >= MAX_CONSECUTIVE_ERRORS;
}

void ThermostatController::resetErrorCount() {
    if (consecutiveErrors > 0) {
        DEBUG_INFO_PRINT("[Controller] 重置錯誤計數（之前: %lu）\n", consecutiveErrors);
        consecutiveErrors = 0;
    }
}