// 條件編譯：在啟用任何調試模式時編譯此文件
#if defined(ENABLE_REMOTE_DEBUG) || defined(ENABLE_LIGHTWEIGHT_DEBUG)

#include "common/RemoteDebugger.h"
#include "common/Debug.h"
#include <Arduino.h>

// Debug.h 中的函數實作 - 生產環境中禁用以節省記憶體
void remoteWebLog(const String& message) {
#ifndef PRODUCTION_BUILD
    RemoteDebugger::getInstance().logSerial(message);
#endif
}

#endif // 調試模式