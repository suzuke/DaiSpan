#pragma once

#include "architecture_v3/core/EventSystemSimple.h"
#include "architecture_v3/domain/ThermostatDomain.h"
#include "controller/IThermostatControl.h"
#include "device/ThermostatDevice.h"

namespace DaiSpan::Migration {

/**
 * V2 到 V3 的橋接適配器
 * 允許逐步遷移而不破壞現有功能
 */
class LegacyProtocolAdapter : public Domain::Thermostat::IProtocolAdapter {
public:
    explicit LegacyProtocolAdapter(IThermostatControl* legacyController)
        : legacyController_(legacyController) {}
    
    Core::Result<Domain::Thermostat::ThermostatState> queryState() override {
        if (!legacyController_) {
            return Core::Result<Domain::Thermostat::ThermostatState>::failure("Legacy controller not available");
        }
        
        // 將舊格式轉換為新格式
        Domain::Thermostat::ThermostatState state;
        state.power = legacyController_->getPower();
        state.currentTemperature = legacyController_->getCurrentTemperature();
        state.targetTemperature = legacyController_->getTargetTemperature();
        
        // 轉換模式
        uint8_t legacyMode = legacyController_->getTargetMode();
        switch (legacyMode) {
            case 0: state.mode = Domain::Thermostat::ThermostatMode::Off; break;
            case 1: state.mode = Domain::Thermostat::ThermostatMode::Heat; break;
            case 2: state.mode = Domain::Thermostat::ThermostatMode::Cool; break;
            case 3: state.mode = Domain::Thermostat::ThermostatMode::Auto; break;
            default: state.mode = Domain::Thermostat::ThermostatMode::Off; break;
        }
        
        return Core::Result<Domain::Thermostat::ThermostatState>::success(state);
    }
    
    Core::Result<void> setPower(bool power) override {
        if (!legacyController_) {
            return Core::Result<void>::failure("Legacy controller not available");
        }
        
        bool success = legacyController_->setPower(power);
        return success ? Core::Result<void>::success() : 
                        Core::Result<void>::failure("Failed to set power");
    }
    
    Core::Result<void> setTargetTemperature(float temperature) override {
        if (!legacyController_) {
            return Core::Result<void>::failure("Legacy controller not available");
        }
        
        bool success = legacyController_->setTargetTemperature(temperature);
        return success ? Core::Result<void>::success() : 
                        Core::Result<void>::failure("Failed to set target temperature");
    }
    
    Core::Result<void> setMode(Domain::Thermostat::ThermostatMode mode) override {
        if (!legacyController_) {
            return Core::Result<void>::failure("Legacy controller not available");
        }
        
        // 轉換新模式到舊格式
        uint8_t legacyMode;
        switch (mode) {
            case Domain::Thermostat::ThermostatMode::Off: legacyMode = 0; break;
            case Domain::Thermostat::ThermostatMode::Heat: legacyMode = 1; break;
            case Domain::Thermostat::ThermostatMode::Cool: legacyMode = 2; break;
            case Domain::Thermostat::ThermostatMode::Auto: legacyMode = 3; break;
            default: legacyMode = 0; break;
        }
        
        bool success = legacyController_->setTargetMode(legacyMode);
        return success ? Core::Result<void>::success() : 
                        Core::Result<void>::failure("Failed to set mode");
    }
    
    Core::Result<void> setFanSpeed(Domain::Thermostat::FanSpeed speed) override {
        if (!legacyController_) {
            return Core::Result<void>::failure("Legacy controller not available");
        }
        
        // 轉換風速設定
        uint8_t legacySpeed = static_cast<uint8_t>(speed);
        bool success = legacyController_->setFanSpeed(legacySpeed);
        return success ? Core::Result<void>::success() : 
                        Core::Result<void>::failure("Failed to set fan speed");
    }
    
    bool isHealthy() const override {
        return legacyController_ != nullptr;
    }
    
    Core::Result<void> resetConnection() override {
        if (!legacyController_) {
            return Core::Result<void>::failure("Legacy controller not available");
        }
        
        // 重置連接邏輯（呼叫 update 來刷新狀態）
        legacyController_->update();
        return Core::Result<void>::success();
    }
    
private:
    IThermostatControl* legacyController_;
};

/**
 * HomeKit 事件橋接器
 * 將 HomeKit 操作轉換為 V3 事件
 */
class HomeKitEventBridge : public Core::EventHandler {
public:
    HomeKitEventBridge(Core::EventPublisher& eventBus, ThermostatDevice* legacyDevice)
        : Core::EventHandler(eventBus), legacyDevice_(legacyDevice) {
        
        // 訂閱 V3 事件並更新舊 HomeKit 裝置
        this->subscribe("StateChanged",
            [this](const Core::DomainEvent& event) {
                // this->updateLegacyHomeKit(event);
            });
        
        this->subscribe("TemperatureUpdated",
            [this](const Core::DomainEvent& event) {
                // this->updateTemperature(event);
            });
    }
    
private:
    void updateLegacyHomeKit(const Domain::Thermostat::Events::StateChanged& event) {
        if (!legacyDevice_) return;
        
        // 將新狀態同步到舊 HomeKit 介面
        // 強制觸發 HomeKit 特性更新
        // legacyDevice_->forceUpdate();
        
        // DEBUG_INFO_PRINT("[V3Bridge] HomeKit 狀態已同步: %s\n", 
        //                  event.changeReason.c_str());
    }
    
    void updateTemperature(const Domain::Thermostat::Events::TemperatureUpdated& event) {
        if (!legacyDevice_) return;
        
        // 只有顯著變化才更新 HomeKit
        if (event.isSignificantChange) {
            // legacyDevice_->updateTemperatureCharacteristics(event.currentTemperature);
        }
    }
    
    ThermostatDevice* legacyDevice_;
};

/**
 * 遷移管理器
 * 協調 V2 和 V3 系統的共存
 */
class MigrationManager {
public:
    MigrationManager(Core::EventPublisher& eventBus, Core::ServiceContainer& container)
        : eventBus_(eventBus), container_(container) {}
    
    /**
     * 初始化遷移環境
     */
    Core::Result<void> initialize(IThermostatControl* legacyController, 
                                 ThermostatDevice* legacyDevice) {
        // 創建橋接適配器
        legacyAdapter_ = std::make_shared<LegacyProtocolAdapter>(legacyController);
        
        // 註冊到服務容器
        container_.registerFactory<Domain::Thermostat::IProtocolAdapter>(
            "ProtocolAdapter",
            [this](Core::ServiceContainer& container) -> std::shared_ptr<Domain::Thermostat::IProtocolAdapter> {
                return legacyAdapter_;
            });
        
        // 創建 HomeKit 橋接器
        homekitBridge_ = std::make_unique<HomeKitEventBridge>(eventBus_, legacyDevice);
        
        // 創建新的恆溫器聚合
        auto protocolAdapter = container_.resolve<Domain::Thermostat::IProtocolAdapter>("ProtocolAdapter");
        thermostatAggregate_ = std::make_shared<Domain::Thermostat::ThermostatAggregate>(
            protocolAdapter, eventBus_);
        
        // 初始化聚合
        auto initResult = thermostatAggregate_->initialize();
        if (initResult.isFailure()) {
            return Core::Result<void>::failure("Failed to initialize thermostat aggregate: " + initResult.getError());
        }
        
        migrationActive_ = true;
        // DEBUG_INFO_PRINT("[Migration] V3 遷移環境初始化完成\n");
        
        return Core::Result<void>::success();
    }
    
    /**
     * 檢查遷移狀態
     */
    bool isMigrationActive() const {
        return migrationActive_;
    }
    
    /**
     * 獲取 V3 恆溫器聚合
     */
    std::shared_ptr<Domain::Thermostat::ThermostatAggregate> getThermostatAggregate() const {
        return thermostatAggregate_;
    }
    
    /**
     * 處理 V3 事件（在主循環中調用）
     */
    void processEvents() {
        if (migrationActive_) {
            eventBus_.processEvents(5); // 每次最多處理 5 個事件
        }
    }
    
private:
    Core::EventPublisher& eventBus_;
    Core::ServiceContainer& container_;
    std::shared_ptr<LegacyProtocolAdapter> legacyAdapter_;
    std::unique_ptr<HomeKitEventBridge> homekitBridge_;
    std::shared_ptr<Domain::Thermostat::ThermostatAggregate> thermostatAggregate_;
    bool migrationActive_ = false;
};

} // namespace DaiSpan::Migration