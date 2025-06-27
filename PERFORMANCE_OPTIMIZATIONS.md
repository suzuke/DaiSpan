# 性能優化記錄

## 問題分析
HomeKit 和 WebServer 存在資源競爭問題，影響配對和網頁響應性能。

## 優化措施

### 1. 主循環處理優先級優化
```cpp
// 原始問題：所有服務同等優先級處理
homeSpan.poll();
webServer->handleClient();
ArduinoOTA.handle();

// 優化後：設定處理優先級和頻率限制
homeSpan.poll();                    // 最高優先級，每次循環
webServer->handleClient();          // 限制每50ms處理一次
ArduinoOTA.handle();               // 限制每100ms處理一次
```

### 2. 記憶體保護機制
```cpp
// 啟動WebServer前檢查可用記憶體
if (ESP.getFreeHeap() < 100000) {
    DEBUG_ERROR_PRINT("記憶體不足，跳過WebServer啟動");
    return;
}
```

### 3. HomeKit配對期間資源調整
```cpp
// 配對期間減少WebServer活動頻率
unsigned long webServerInterval = homeKitPairingActive ? 200 : 50;
```

### 4. 非阻塞初始化
```cpp
// 原始：阻塞式延遲
delay(2000);
initializeMonitoring();

// 優化：非阻塞式延遲
if (millis() - homeKitInitTime >= 2000 && !monitoringEnabled) {
    initializeMonitoring();
}
```

### 5. TCP連接數優化
- HomeKit使用端口1201
- WebServer使用端口8080
- 分離端口避免連接衝突

### 6. 詳細狀態監控
增加記憶體使用量、WebServer狀態、配對狀態的監控日誌。

## 預期效果
1. HomeKit配對響應速度提升
2. WebServer響應更穩定
3. 減少記憶體不足導致的重啟
4. 避免TCP連接數耗盡
5. 更好的系統資源分配

## 監控指標
- 可用記憶體 (目標：>100KB)
- WiFi信號強度
- WebServer響應時間
- HomeKit配對成功率