#pragma once

#include "core/EventSystem.h"
#include "core/Result.h"
#include <chrono>
#include <optional>

namespace DaiSpan::Domain::Thermostat {

/**
 * 恆溫器模式定義
 */
enum class ThermostatMode : uint8_t {
    Off = 0,
    Heat = 1,
    Cool = 2,
    Auto = 3
};

/**
 * 風速等級定義
 */
enum class FanSpeed : uint8_t {
    Auto = 0,
    Low = 1,
    Medium = 3,
    High = 5
};

/**
 * 恆溫器狀態值對象
 * 不可變的狀態表示，包含所有必要資訊
 */
struct ThermostatState {
    bool power = false;
    float currentTemperature = 21.0f;
    float targetTemperature = 21.0f;
    ThermostatMode mode = ThermostatMode::Off;
    FanSpeed fanSpeed = FanSpeed::Auto;
    std::chrono::steady_clock::time_point lastUpdate = std::chrono::steady_clock::now();
    
    /**
     * 狀態驗證
     */
    bool isValid() const {
        return currentTemperature >= -20.0f && currentTemperature <= 60.0f &&
               targetTemperature >= 16.0f && targetTemperature <= 30.0f;
    }
    
    /**
     * 檢查是否有顯著變化（避免頻繁更新）
     */
    bool hasSignificantChange(const ThermostatState& other) const {
        constexpr float V3_TEMP_THRESHOLD = 0.1f;
        
        return power != other.power ||
               mode != other.mode ||
               fanSpeed != other.fanSpeed ||
               std::abs(currentTemperature - other.currentTemperature) >= V3_TEMP_THRESHOLD ||
               std::abs(targetTemperature - other.targetTemperature) >= V3_TEMP_THRESHOLD;
    }
    
    /**
     * 計算狀態更新後經過的時間
     */
    std::chrono::milliseconds getTimeSinceUpdate() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - lastUpdate);
    }
    
    /**
     * 創建新狀態的便利方法
     */
    ThermostatState withPower(bool newPower) const {
        auto newState = *this;
        newState.power = newPower;
        newState.lastUpdate = std::chrono::steady_clock::now();
        return newState;
    }
    
    ThermostatState withTargetTemperature(float newTemp) const {
        auto newState = *this;
        newState.targetTemperature = newTemp;
        newState.lastUpdate = std::chrono::steady_clock::now();
        return newState;
    }
    
    ThermostatState withMode(ThermostatMode newMode) const {
        auto newState = *this;
        newState.mode = newMode;
        newState.lastUpdate = std::chrono::steady_clock::now();
        return newState;
    }
};

/**
 * 恆溫器命令定義
 */
namespace Commands {
    struct SetPower {
        bool power;
        std::string source; // "homekit", "web", "api"
    };
    
    struct SetTargetTemperature {
        float temperature;
        std::string source;
    };
    
    struct SetMode {
        ThermostatMode mode;
        std::string source;
    };
    
    struct SetFanSpeed {
        FanSpeed speed;
        std::string source;
    };
}

/**
 * 恆溫器事件定義
 */
namespace Events {
    /**
     * 狀態變化事件 - 系統的核心事件
     */
    struct StateChanged : public Core::DomainEvent {
        static constexpr const char* EVENT_TYPE_NAME = "StateChanged";
        const char* getEventTypeName() const override { return EVENT_TYPE_NAME; }
        ThermostatState previousState;
        ThermostatState currentState;
        std::string changeReason; // "user_command", "protocol_update", "auto_adjustment"
        
        StateChanged(const ThermostatState& prev, const ThermostatState& current, const std::string& reason)
            : previousState(prev), currentState(current), changeReason(reason) {}
    };
    
    /**
     * 命令接收事件
     */
    struct CommandReceived : public Core::DomainEvent {
        static constexpr const char* EVENT_TYPE_NAME = "CommandReceived";
        const char* getEventTypeName() const override { return EVENT_TYPE_NAME; }
        enum class Type { Power, Temperature, Mode, FanSpeed };
        Type commandType;
        std::string source;
        std::string details;
        
        CommandReceived(Type type, const std::string& src, const std::string& det)
            : commandType(type), source(src), details(det) {}
    };
    
    /**
     * 溫度更新事件
     */
    struct TemperatureUpdated : public Core::DomainEvent {
        static constexpr const char* EVENT_TYPE_NAME = "TemperatureUpdated";
        const char* getEventTypeName() const override { return EVENT_TYPE_NAME; }
        float previousTemperature;
        float currentTemperature;
        bool isSignificantChange;
        
        TemperatureUpdated(float prev, float current, bool significant)
            : previousTemperature(prev), currentTemperature(current), isSignificantChange(significant) {}
    };
    
    /**
     * 設備準備就緒事件
     */
    struct DeviceReady : public Core::DomainEvent {
        static constexpr const char* EVENT_TYPE_NAME = "DeviceReady";
        const char* getEventTypeName() const override { return EVENT_TYPE_NAME; }
        ThermostatState initialState;
        
        explicit DeviceReady(const ThermostatState& state) : initialState(state) {}
    };
    
    /**
     * 錯誤事件
     */
    struct Error : public Core::DomainEvent {
        static constexpr const char* EVENT_TYPE_NAME = "Error";
        const char* getEventTypeName() const override { return EVENT_TYPE_NAME; }
        enum class Type { 
            ProtocolError, 
            ConfigurationError, 
            ValidationError, 
            HardwareError 
        };
        
        Type errorType;
        std::string message;
        std::optional<ThermostatState> lastKnownGoodState;
        
        Error(Type type, const std::string& msg, 
              const std::optional<ThermostatState>& state = std::nullopt)
            : errorType(type), message(msg), lastKnownGoodState(state) {}
    };
}

/**
 * 協議適配器抽象介面
 * 解耦業務邏輯與具體協議實作
 */
class IProtocolAdapter {
public:
    virtual ~IProtocolAdapter() = default;
    
    /**
     * 查詢設備當前狀態
     */
    virtual Core::Result<ThermostatState> queryState() = 0;
    
    /**
     * 發送電源命令
     */
    virtual Core::Result<void> setPower(bool power) = 0;
    
    /**
     * 發送溫度設定命令
     */
    virtual Core::Result<void> setTargetTemperature(float temperature) = 0;
    
    /**
     * 發送模式設定命令
     */
    virtual Core::Result<void> setMode(ThermostatMode mode) = 0;
    
    /**
     * 發送風速設定命令
     */
    virtual Core::Result<void> setFanSpeed(FanSpeed speed) = 0;
    
    /**
     * 檢查協議適配器健康狀態
     */
    virtual bool isHealthy() const = 0;
    
    /**
     * 重置連接
     */
    virtual Core::Result<void> resetConnection() = 0;
};

/**
 * 恆溫器聚合根
 * 封裝所有恆溫器相關的業務邏輯
 */
class ThermostatAggregate {
public:
    ThermostatAggregate(std::shared_ptr<IProtocolAdapter> protocol,
                       Core::EventPublisher& eventBus)
        : protocol_(std::move(protocol)), eventBus_(eventBus) {
        
        // 初始化狀態
        currentState_ = ThermostatState{};
    }
    
    /**
     * 初始化設備連接
     */
    Core::Result<void> initialize() {
        auto result = protocol_->queryState();
        if (result.isSuccess()) {
            auto newState = result.getValue();
            if (newState.isValid()) {
                currentState_ = newState;
                eventBus_.publish(Events::DeviceReady(currentState_));
                return Core::Result<void>::success();
            }
        }
        
        auto error = Events::Error(Events::Error::Type::ProtocolError, 
                                  "Failed to initialize device connection");
        eventBus_.publish(error);
        return Core::Result<void>::failure("Initialization failed");
    }
    
    /**
     * 設定電源狀態
     */
    Core::Result<void> setPower(const Commands::SetPower& command) {
        // 發布命令接收事件
        eventBus_.publish(Events::CommandReceived(
            Events::CommandReceived::Type::Power, 
            command.source, 
            command.power ? "ON" : "OFF"));
        
        // 驗證命令
        if (!isValidPowerCommand(command)) {
            return Core::Result<void>::failure("Invalid power command");
        }
        
        // 執行協議命令
        auto result = protocol_->setPower(command.power);
        if (result.isFailure()) {
            eventBus_.publish(Events::Error(Events::Error::Type::ProtocolError, 
                                          result.getError(), currentState_));
            return result;
        }
        
        // 更新狀態並發布事件
        auto newState = currentState_.withPower(command.power);
        publishStateChange(newState, "user_command");
        
        return Core::Result<void>::success();
    }
    
    /**
     * 設定目標溫度
     */
    Core::Result<void> setTargetTemperature(const Commands::SetTargetTemperature& command) {
        eventBus_.publish(Events::CommandReceived(
            Events::CommandReceived::Type::Temperature, 
            command.source, 
            std::to_string(command.temperature) + "°C"));
        
        // 溫度範圍驗證
        if (command.temperature < 16.0f || command.temperature > 30.0f) {
            return Core::Result<void>::failure("Temperature out of range (16-30°C)");
        }
        
        auto result = protocol_->setTargetTemperature(command.temperature);
        if (result.isFailure()) {
            eventBus_.publish(Events::Error(Events::Error::Type::ProtocolError, 
                                          result.getError(), currentState_));
            return result;
        }
        
        auto newState = currentState_.withTargetTemperature(command.temperature);
        publishStateChange(newState, "user_command");
        
        return Core::Result<void>::success();
    }
    
    /**
     * 定期更新狀態（替代輪詢）
     * 這個方法應該由事件觸發，而不是定時器
     */
    void updateFromProtocol() {
        if (!protocol_->isHealthy()) {
            return; // 協議不健康，跳過更新
        }
        
        auto result = protocol_->queryState();
        if (result.isSuccess()) {
            auto newState = result.getValue();
            
            // 只在有顯著變化時才更新和發布事件
            if (currentState_.hasSignificantChange(newState)) {
                publishStateChange(newState, "protocol_update");
            }
        }
    }
    
    /**
     * 獲取當前狀態（只讀）
     */
    const ThermostatState& getCurrentState() const {
        return currentState_;
    }
    
    /**
     * 檢查設備是否準備就緒
     */
    bool isReady() const {
        return protocol_->isHealthy() && currentState_.isValid();
    }

private:
    std::shared_ptr<IProtocolAdapter> protocol_;
    Core::EventPublisher& eventBus_;
    ThermostatState currentState_;
    
    /**
     * 發布狀態變化事件
     */
    void publishStateChange(const ThermostatState& newState, const std::string& reason) {
        auto previousState = currentState_;
        currentState_ = newState;
        
        eventBus_.publish(Events::StateChanged(previousState, currentState_, reason));
        
        // 如果溫度有顯著變化，額外發布溫度事件
        constexpr float V3_TEMP_THRESHOLD_SIGNIFICANT = 0.5f;
        if (std::abs(previousState.currentTemperature - newState.currentTemperature) >= V3_TEMP_THRESHOLD_SIGNIFICANT) {
            bool isSignificant = std::abs(previousState.currentTemperature - newState.currentTemperature) >= 1.0f;
            eventBus_.publish(Events::TemperatureUpdated(
                previousState.currentTemperature, 
                newState.currentTemperature, 
                isSignificant));
        }
    }
    
    /**
     * 驗證電源命令
     */
    bool isValidPowerCommand(const Commands::SetPower& command) const {
        // 業務規則：可以根據需要添加更複雜的驗證
        return true;
    }
};

} // namespace DaiSpan::Domain::Thermostat