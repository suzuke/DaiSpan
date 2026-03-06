#include "device/SwingDevice.h"

SwingSwitchService::SwingSwitchService(IThermostatControl& ctrl,
                                       IACProtocol::SwingAxis axis,
                                       const char* displayName)
    : Service::Switch(),
      controller(ctrl),
      swingAxis(axis),
      onCharacteristic(nullptr),
      lastSyncTime(0),
      axisSupported(ctrl.supportsSwing(axis)) {

    new Characteristic::Name(displayName);
    onCharacteristic = new Characteristic::On(false);

    if (axisSupported) {
        bool current = readCurrentState();
        onCharacteristic->setVal(current);
        DEBUG_INFO_PRINT("[SwingSwitch] %s 初始化: %s\n", displayName, current ? "開啟" : "關閉");
    } else {
        DEBUG_INFO_PRINT("[SwingSwitch] %s 不支援\n", displayName);
    }
}

boolean SwingSwitchService::update() {
    if (!axisSupported) {
        onCharacteristic->setVal(false);
        return true;
    }

    if (onCharacteristic->updated()) {
        bool desired = onCharacteristic->getNewVal();
        if (!controller.setSwing(swingAxis, desired)) {
            bool current = readCurrentState();
            onCharacteristic->setVal(current);
            DEBUG_WARN_PRINT("[SwingSwitch] 擺風指令失敗，恢復為 %s\n",
                             current ? "開啟" : "關閉");
        } else {
            onCharacteristic->setVal(desired);
            DEBUG_INFO_PRINT("[SwingSwitch] 擺風狀態更新為 %s\n",
                             desired ? "開啟" : "關閉");
        }
        lastSyncTime = millis();
    }

    return true;
}

void SwingSwitchService::loop() {
    if (!axisSupported) return;

    unsigned long now = millis();
    if (now - lastSyncTime < SYNC_INTERVAL_MS) return;
    lastSyncTime = now;

    bool current = readCurrentState();
    if (onCharacteristic->getVal() != current) {
        onCharacteristic->setVal(current);
        onCharacteristic->timeVal();
        DEBUG_VERBOSE_PRINT("[SwingSwitch] 同步擺風狀態為 %s\n",
                            current ? "開啟" : "關閉");
    }
}

bool SwingSwitchService::readCurrentState() const {
    return controller.getSwing(swingAxis);
}
