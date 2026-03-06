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
      lastUserFanSpeed(AC_FAN_AUTO),
      lastModeSetTime(0),
      lastUserMode(AC_MODE_AUTO),
      targetHomeKitMode(HAP_MODE_AUTO) {
    
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
      lastSuccessfulUpdate(other.lastSuccessfulUpdate),
      lastFanSpeedSetTime(other.lastFanSpeedSetTime),
      lastUserFanSpeed(other.lastUserFanSpeed),
      lastModeSetTime(other.lastModeSetTime),
      lastUserMode(other.lastUserMode),
      targetHomeKitMode(other.targetHomeKitMode),
      dirtyPower(other.dirtyPower),
      dirtyMode(other.dirtyMode),
      dirtyTemp(other.dirtyTemp),
      dirtyFan(other.dirtyFan) {
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
        lastFanSpeedSetTime = other.lastFanSpeedSetTime;
        lastUserFanSpeed = other.lastUserFanSpeed;
        lastModeSetTime = other.lastModeSetTime;
        lastUserMode = other.lastUserMode;
        targetHomeKitMode = other.targetHomeKitMode;
        dirtyPower = other.dirtyPower;
        dirtyMode = other.dirtyMode;
        dirtyTemp = other.dirtyTemp;
        dirtyFan = other.dirtyFan;
    }
    return *this;
}

bool ThermostatController::setPower(bool on) {
    if (!protocol) return false;

    DEBUG_INFO_PRINT("[Controller] 設置電源狀態：%s\n", on ? "開啟" : "關閉");
    power = on;
    dirtyPower = true;

    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 恢復模式中，已標記待同步\n");
        return true;
    }

    bool success = protocol->setPowerAndMode(on, mode, targetTemperature, fanSpeed);
    if (success) {
        dirtyPower = false;
        resetErrorCount();
        lastSuccessfulUpdate = millis();
    } else {
        handleProtocolError("setPower");
    }
    return success;
}

bool ThermostatController::setTargetMode(uint8_t newMode) {
    if (!protocol) return false;

    uint8_t acMode = convertHomeKitToACMode(newMode);
    DEBUG_INFO_PRINT("[Controller] 設置目標模式：HomeKit=%d -> AC=%d\n", newMode, acMode);

    if (!protocol->supportsMode(acMode)) {
        DEBUG_ERROR_PRINT("[Controller] 協議不支持模式 %d\n", acMode);
        return false;
    }

    if (newMode == HAP_MODE_OFF) {
        return setPower(false);
    }

    mode = acMode;
    targetHomeKitMode = newMode;
    lastModeSetTime = millis();
    lastUserMode = acMode;
    dirtyMode = true;

    if (!power && !setPower(true)) {
        return false;
    }

    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 恢復模式中，已標記待同步\n");
        return true;
    }

    bool success = protocol->setPowerAndMode(power, acMode, targetTemperature, fanSpeed);
    if (success) {
        dirtyMode = false;
        dirtyPower = false; // setPowerAndMode 同時送出
        resetErrorCount();
        lastSuccessfulUpdate = millis();
        DEBUG_INFO_PRINT("[Controller] 模式設置成功：%d\n", newMode);
    } else {
        handleProtocolError("setTargetMode");
    }
    return success;
}

bool ThermostatController::setTargetTemperature(float temperature) {
    if (!protocol) return false;

    auto tempRange = protocol->getTemperatureRange();
    if (temperature < tempRange.first || temperature > tempRange.second || isnan(temperature)) {
        DEBUG_ERROR_PRINT("[Controller] 無效溫度 %.1f°C (範圍: %.1f-%.1f°C)\n",
                          temperature, tempRange.first, tempRange.second);
        return false;
    }

    DEBUG_INFO_PRINT("[Controller] 設置目標溫度：%.1f°C\n", temperature);
    targetTemperature = temperature;
    dirtyTemp = true;

    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 恢復模式中，已標記待同步\n");
        return true;
    }

    bool success = protocol->setTemperature(temperature);
    if (success) {
        dirtyTemp = false;
        resetErrorCount();
        lastSuccessfulUpdate = millis();
    } else {
        handleProtocolError("setTargetTemperature");
    }
    return success;
}

bool ThermostatController::setFanSpeed(uint8_t speed) {
    if (!protocol) return false;

    if (!protocol->supportsFanSpeed(speed)) {
        DEBUG_ERROR_PRINT("[Controller] 協議不支持風速 %d\n", speed);
        return false;
    }

    DEBUG_INFO_PRINT("[Controller] 設置風速：%d (%s)\n", speed, getFanSpeedText(speed));
    fanSpeed = speed;
    lastFanSpeedSetTime = millis();
    lastUserFanSpeed = speed;
    dirtyFan = true;

    if (isInErrorRecoveryMode()) {
        DEBUG_WARN_PRINT("[Controller] 恢復模式中，已標記待同步\n");
        return true;
    }

    bool success = protocol->setPowerAndMode(power, mode, targetTemperature, speed);
    if (success) {
        dirtyFan = false;
        resetErrorCount();
        lastSuccessfulUpdate = millis();
    } else {
        handleProtocolError("setFanSpeed");
    }
    return success;
}

void ThermostatController::syncDirtyState() {
    if (!dirtyPower && !dirtyMode && !dirtyTemp && !dirtyFan) return;

    DEBUG_INFO_PRINT("[Controller] 同步待發送狀態 (P:%d M:%d T:%d F:%d)\n",
                     dirtyPower, dirtyMode, dirtyTemp, dirtyFan);

    // 用一次 setPowerAndMode 送出電源+模式+溫度+風速
    if (dirtyPower || dirtyMode || dirtyFan) {
        if (protocol->setPowerAndMode(power, mode, targetTemperature, fanSpeed)) {
            dirtyPower = false;
            dirtyFan = false;
            dirtyTemp = false; // setPowerAndMode 已包含溫度
            if (dirtyMode) {
                lastModeSetTime = millis(); // 只在模式變更時重啟保護期
                lastUserMode = mode;
                dirtyMode = false;
            }
            resetErrorCount();
            lastSuccessfulUpdate = millis();
            DEBUG_INFO_PRINT("[Controller] 狀態同步成功\n");
        } else {
            handleProtocolError("syncDirtyState");
            return;
        }
    }

    if (dirtyTemp) {
        if (protocol->setTemperature(targetTemperature)) {
            dirtyTemp = false;
            resetErrorCount();
            lastSuccessfulUpdate = millis();
        } else {
            handleProtocolError("syncDirtyTemp");
        }
    }
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
    
    // 恢復後先同步待發送的狀態
    syncDirtyState();
    if (isInErrorRecoveryMode()) return; // 同步失敗，等下次恢復

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
            
            // 用戶互動保護：冷暖切換時 AC 需要較長時間切換模式
            const unsigned long MODE_PROTECTION_PERIOD = 30000; // 30秒保護期

            if (lastModeSetTime > 0 && (currentTime - lastModeSetTime) < MODE_PROTECTION_PERIOD) {
                DEBUG_INFO_PRINT("[Controller] 用戶模式保護期內，跳過模式更新 (剩餘: %lu ms, 用戶設置: %d, AC回報: %d)\n",
                                MODE_PROTECTION_PERIOD - (currentTime - lastModeSetTime),
                                lastUserMode, status.mode);
                // 保持用戶設置的模式
                mode = lastUserMode;
            } else {
                // 保護期外，正常更新模式
                if (mode != status.mode) {
                    DEBUG_INFO_PRINT("[Controller] 模式從AC狀態更新：%d -> %d\n", mode, status.mode);
                }
                mode = status.mode;
                // 只有在 AC mode 能無損轉換為 HomeKit mode 時才更新 targetHomeKitMode
                // DRY/FAN 模式在 HomeKit 沒有對應，保留用戶原始設定
                switch (status.mode) {
                    case AC_MODE_HEAT: targetHomeKitMode = HAP_MODE_HEAT; break;
                    case AC_MODE_COOL: targetHomeKitMode = HAP_MODE_COOL; break;
                    case AC_MODE_AUTO:
                    case AC_MODE_AUTO_2:
                    case AC_MODE_AUTO_3: targetHomeKitMode = HAP_MODE_AUTO; break;
                    // DRY, FAN: 不更新 targetHomeKitMode，保留用戶意圖
                }
            }
            
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

bool ThermostatController::supportsSwing(IACProtocol::SwingAxis axis) const {
    return protocol ? protocol->supportsSwing(axis) : false;
}

bool ThermostatController::setSwing(IACProtocol::SwingAxis axis, bool enabled) {
    if (!protocol) return false;
    DEBUG_INFO_PRINT("[Controller] 設置擺風: axis=%d, enabled=%d\n", (int)axis, enabled);
    return protocol->setSwing(axis, enabled);
}

bool ThermostatController::getSwing(IACProtocol::SwingAxis axis) const {
    return protocol ? protocol->getSwing(axis) : false;
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