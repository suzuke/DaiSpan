# 主迴圈重構總結

## 🎯 重構目標

將複雜的 `main.cpp` 主迴圈（200+ 行）重構為模組化、可維護的架構，提升代碼可讀性和可測試性。

## 📊 重構前後對比

### 重構前
```cpp
void loop() {
  // 200+ 行的複雜邏輯
  // - ESP32-C3 WiFi 功率管理
  // - OTA 處理
  // - HomeKit 配對檢測
  // - WebServer 處理
  // - 配置模式邏輯
  // - 心跳信息輸出
  // 多個靜態變數分散在各處
}
```

### 重構後
```cpp
void loop() {
  if (systemManager) {
    systemManager->processMainLoop();
    
    // 檢查監控啟動請求
    if (systemManager->shouldStartMonitoring()) {
      initializeMonitoring();
    }
    
    // 處理配置模式
    if (wifiManager) {
      // 配置邏輯...
    }
  } else {
    // 降級模式
  }
}
```

## 🏗️ 新增架構

### SystemManager 類別
**文件位置**: `include/common/SystemManager.h`, `src/SystemManager.cpp`

#### 核心功能
1. **狀態管理**: 統一管理所有主迴圈相關的狀態
2. **組件協調**: 協調 HomeKit、WebServer、OTA 等組件
3. **智能調度**: 根據系統狀態動態調整處理頻率
4. **錯誤恢復**: 提供降級模式保證系統穩定性

#### 主要方法
```cpp
class SystemManager {
public:
    void processMainLoop();                    // 主迴圈處理
    bool shouldStartMonitoring() const;       // 檢查監控啟動需求
    void getSystemStats(...);                 // 獲取系統統計
    void resetState();                        // 重置系統狀態
    
private:
    void handleESP32C3PowerManagement();      // ESP32-C3 功率管理
    void handleOTAUpdates();                  // OTA 更新處理
    void handleHomeKitMode();                 // HomeKit 模式處理
    void handleWebServerStartup();           // WebServer 啟動管理
    void handleHomeKitPairingDetection();    // 配對檢測
    void handleWebServerProcessing();        // WebServer 處理
    void handlePeriodicTasks();              // 定期任務
};
```

## 📋 模組化設計

### 1. 狀態統一管理
```cpp
struct SystemState {
    unsigned long lastLoopTime;
    unsigned long lastPowerCheck;
    unsigned long lastOTAHandle;
    unsigned long lastPairingCheck;
    // ... 其他狀態變數
};
```

### 2. 功能分離
- **ESP32-C3 電源管理**: `handleESP32C3PowerManagement()`
- **OTA 處理**: `handleOTAUpdates()`
- **HomeKit 處理**: `handleHomeKitMode()`
- **WebServer 管理**: `handleWebServerProcessing()`
- **配對檢測**: `handleHomeKitPairingDetection()`

### 3. 智能調度
- **動態頻率控制**: 根據記憶體狀況調整 WebServer 處理頻率
- **優先級管理**: HomeKit > OTA > WebServer
- **資源保護**: 記憶體不足時自動跳過非關鍵處理

## 🔧 技術改進

### 記憶體管理優化
```cpp
// 動態調整 WebServer 處理間隔
unsigned long calculateWebServerInterval(uint32_t freeMemory) {
    if (freeMemory < 60000) return 200;      // 記憶體緊張
    if (freeMemory < 80000) return 100;      // 中等記憶體
    return 50;                               // 記憶體充足
}
```

### 配對檢測改進
```cpp
// 更穩定的配對檢測邏輯
void updatePairingDetection(uint32_t currentMemory) {
    static uint32_t avgMemory = currentMemory;
    avgMemory = (avgMemory * 3 + currentMemory) / 4;  // 移動平均
    
    bool significantMemoryDrop = currentMemory < avgMemory - 20000;
    // 連續檢測避免誤判
}
```

### 錯誤恢復機制
```cpp
// 降級處理模式
if (!systemManager) {
    // 基本功能保證
    ArduinoOTA.handle();
    homeSpan.poll();
    wifiManager->loop();
}
```

## 📈 性能提升

### 1. CPU 使用優化
- **減少重複計算**: 靜態變數集中管理
- **智能調度**: 非關鍵任務動態調整頻率
- **避免阻塞**: 所有處理都是非阻塞的

### 2. 記憶體使用優化
- **狀態集中**: 減少分散的靜態變數
- **動態調整**: 根據記憶體狀況調整行為
- **避免重複包含**: 解決了 WebUI 多重定義問題

### 3. 系統穩定性
- **降級模式**: SystemManager 失效時的備用處理
- **錯誤隔離**: 各模組獨立處理，避免級聯故障
- **狀態恢復**: 提供重置功能

## 🧪 可測試性改進

### 1. 模組分離
- 每個功能模組可以獨立測試
- 狀態管理與業務邏輯分離
- 依賴注入設計便於 mock 測試

### 2. 狀態可觀測
```cpp
void getSystemStats(String& mode, String& wifiStatus, 
                   String& deviceStatus, String& ipAddress);
```

### 3. 可控制的啟動流程
```cpp
bool shouldStartMonitoring() const;  // 外部可檢查狀態
void resetState();                   // 可重置狀態進行測試
```

## 📝 編碼改進

### 1. 可讀性
- **清晰的函數命名**: `handleESP32C3PowerManagement`
- **邏輯分組**: 相關功能集中在同一方法中
- **常數定義**: 使用具名常數替代魔法數字

### 2. 可維護性
- **單一職責**: 每個方法只負責一個功能
- **低耦合**: 避免直接調用外部函數
- **高內聚**: 相關狀態和邏輯集中管理

### 3. 擴展性
- **介面設計**: 易於添加新的處理模組
- **配置化**: 常數可以輕易調整
- **插件化**: 新功能可以作為新的處理方法添加

## 📊 編譯結果

```
RAM:   [==        ]  16.3% (used 53380 bytes from 327680 bytes)
Flash: [========= ]  91.1% (used 1552544 bytes from 1703936 bytes)
✅ 編譯成功
```

## 🎉 重構成果

### ✅ 達成目標
1. **代碼行數減少**: 主迴圈從 200+ 行減少到 40 行
2. **模組化設計**: 功能清晰分離，便於維護
3. **性能優化**: 智能調度和資源管理
4. **穩定性提升**: 錯誤恢復和降級機制
5. **可測試性**: 狀態和邏輯分離

### 🔄 後續優化建議
1. **單元測試**: 為 SystemManager 添加測試
2. **配置文件**: 將常數移到配置文件
3. **監控指標**: 添加性能監控
4. **日誌系統**: 結構化日誌輸出
5. **狀態持久化**: 關鍵狀態的持久化存儲

---

**重構完成時間**: 2025年7月5日  
**重構影響**: 主要影響 `main.cpp` 和新增 `SystemManager` 類別  
**向後兼容**: 完全兼容現有功能和介面