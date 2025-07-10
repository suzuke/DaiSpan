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
      lastSuccessfulUpdate(0),
      lastFanSpeedSetTime(0),
      lastUserFanSpeed(AC_FAN_AUTO) {
    
    if (!protocol) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：協議實例為空\n");
        return;
    }
    
    DEBUG_INFO_PRINT("[Controller] 開始初始化通用控制器 - 協議: %s\n", 
                      protocol->getProtocolName());
    
    // 協議已在工廠中初始化，直接檢查狀態
    if (protocol->isLastOperationSuccessful()) {
        DEBUG_INFO_PRINT("[Controller] 協議已就緒\n");
        update(); // 初始化時更新一次狀態
    } else {
        DEBUG_WARN_PRINT("[Controller] 協議可能未完全就緒，將在運行時嘗試恢復\n");
        consecutiveErrors = 0; // 允許嘗試操作
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
    
    // 允許 HomeKit 操作嘗試，即使在輕微錯誤狀態下
    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 警告：錯誤恢復模式中，嘗試執行電源操作\n");
        // 不直接返回 false，而是嘗試操作，讓協議層決定是否成功
    }
    
    DEBUG_INFO_PRINT("[Controller] 設置電源狀態：%s\n", on ? "開啟" : "關閉");
    
    // 使用抽象協議介面設置電源和模式
    bool success = protocol->setPowerAndMode(on, mode, targetTemperature, fanSpeed);
    
    if (success) {
        power = on;
        resetErrorCount();
        lastSuccessfulUpdate = millis(); // 記錄成功操作時間
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
    
    // 允許 HomeKit 操作嘗試，即使在輕微錯誤狀態下
    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 警告：錯誤恢復模式中，嘗試執行模式操作\n");
        // 不直接返回 false，而是嘗試操作，讓協議層決定是否成功
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
        lastSuccessfulUpdate = millis(); // 記錄成功操作時間
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
    
    // 允許 HomeKit 操作嘗試，即使在輕微錯誤狀態下
    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 警告：錯誤恢復模式中，嘗試執行溫度操作\n");
        // 不直接返回 false，而是嘗試操作，讓協議層決定是否成功
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
        lastSuccessfulUpdate = millis(); // 記錄成功操作時間
        DEBUG_INFO_PRINT("[Controller] 目標溫度設置成功\n");
    } else {
        handleProtocolError("setTargetTemperature");
    }
    
    return success;
}

bool ThermostatController::setFanSpeed(uint8_t speed) {
    if (!protocol) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：協議實例無效\n");
        return false;
    }
    
    // 允許 HomeKit 操作嘗試，即使在輕微錯誤狀態下
    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 警告：錯誤恢復模式中，嘗試執行風速操作\n");
        // 不直接返回 false，而是嘗試操作，讓協議層決定是否成功
    }
    
    // 檢查協議是否支持該風速
    if (!protocol->supportsFanSpeed(speed)) {
        DEBUG_ERROR_PRINT("[Controller] 錯誤：協議不支持風速 %d\n", speed);
        return false;
    }
    
    DEBUG_INFO_PRINT("[Controller] 設置風速：%d (%s)\n", speed, getFanSpeedText(speed));
    
    // 使用抽象協議介面設置風速
    bool success = protocol->setPowerAndMode(power, mode, targetTemperature, speed);
    
    if (success) {
        fanSpeed = speed;
        // 記錄用戶風速設置時間和值，防止被queryStatus覆蓋
        lastFanSpeedSetTime = millis();
        lastUserFanSpeed = speed;
        resetErrorCount();
        lastSuccessfulUpdate = millis(); // 記錄成功操作時間
        DEBUG_INFO_PRINT("[Controller] 風速設置成功：%s (記錄用戶設置時間: %lu)\n", 
                        getFanSpeedText(speed), lastFanSpeedSetTime);
    } else {
        handleProtocolError("setFanSpeed");
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
            // 在錯誤恢復期間，大幅減少更新頻率並減少日誌輸出
            static unsigned long lastRecoveryLog = 0;
            if (currentTime - lastRecoveryLog > 60000) { // 每分鐘最多輸出一次日誌
                DEBUG_INFO_PRINT("[Controller] 錯誤恢復模式中，剩餘時間：%lu秒\n", 
                                (ERROR_RECOVERY_INTERVAL - (currentTime - lastSuccessfulUpdate)) / 1000);
                lastRecoveryLog = currentTime;
            }
            return; // 仍在恢復期間
        }
    }
    
    DEBUG_VERBOSE_PRINT("[Controller] 開始更新狀態\n");
    
    // 計數本次更新的成功操作
    int successfulOperations = 0;
    int totalOperations = 0;
    
    // 查詢設備狀態
    totalOperations++;
    ACStatus status;
    if (protocol->queryStatus(status)) {
        if (status.isValid) {
            power = status.power;
            mode = status.mode;
            targetTemperature = status.targetTemperature;
            
            // 用戶互動保護：如果用戶在10秒內設置過風速，不要被AC狀態覆蓋
            const unsigned long FAN_SPEED_PROTECTION_PERIOD = 10000; // 10秒保護期
            unsigned long timeSinceLastFanSet = currentTime - lastFanSpeedSetTime;
            
            if (lastFanSpeedSetTime > 0 && timeSinceLastFanSet < FAN_SPEED_PROTECTION_PERIOD) {
                DEBUG_INFO_PRINT("[Controller] 用戶風速保護期內，跳過風速更新 (剩餘: %lu ms, 用戶設置: %s, AC回報: %s)\n",
                                FAN_SPEED_PROTECTION_PERIOD - timeSinceLastFanSet,
                                getFanSpeedText(lastUserFanSpeed), getFanSpeedText(status.fanSpeed));
                // 保持用戶設置的風速
                fanSpeed = lastUserFanSpeed;
            } else {
                // 保護期外，正常更新風速
                if (fanSpeed != status.fanSpeed) {
                    DEBUG_INFO_PRINT("[Controller] 風速從AC狀態更新：%s -> %s\n", 
                                    getFanSpeedText(fanSpeed), getFanSpeedText(status.fanSpeed));
                }
                fanSpeed = status.fanSpeed;
            }
            
            DEBUG_VERBOSE_PRINT("[Controller] 狀態更新成功 - 電源：%s，模式：%d，目標溫度：%.1f°C，風速：%s\n",
                               power ? "開啟" : "關閉", mode, targetTemperature, getFanSpeedText(fanSpeed));
            successfulOperations++;
        }
    } else {
        handleProtocolError("queryStatus");
    }
    
    // 只有在狀態查詢成功時才查詢溫度（減少不必要的協議調用）
    if (successfulOperations > 0) {
        totalOperations++;
        float newTemperature;
        if (protocol->queryTemperature(newTemperature)) {
            currentTemperature = newTemperature;
            DEBUG_VERBOSE_PRINT("[Controller] 溫度更新成功：%.1f°C\n", currentTemperature);
            successfulOperations++;
        } else {
            handleProtocolError("queryTemperature");
        }
    }
    
    // 統一處理錯誤計數：只有所有操作都成功才重置
    if (successfulOperations == totalOperations && successfulOperations > 0) {
        resetErrorCount();
        lastSuccessfulUpdate = currentTime;
        DEBUG_VERBOSE_PRINT("[Controller] 所有操作成功，重置錯誤計數\n");
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