#pragma once

#include "HomeSpan.h"
#include "../controller/IThermostatControl.h"
#include "../protocol/IACProtocol.h"
#include "../common/Debug.h"

class SwingSwitchService : public Service::Switch {
public:
    SwingSwitchService(IThermostatControl& ctrl,
                       IACProtocol::SwingAxis axis,
                       const char* displayName);
    
    boolean update() override;
    void loop() override;
    
private:
    IThermostatControl& controller;
    IACProtocol::SwingAxis swingAxis;
    SpanCharacteristic* onCharacteristic;
    unsigned long lastSyncTime;
    bool axisSupported;
    
    static constexpr unsigned long SYNC_INTERVAL_MS = 1000;
    
    bool readCurrentState() const;
    void refreshSupportFlag();
};
