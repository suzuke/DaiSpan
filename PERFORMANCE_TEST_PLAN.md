# 性能測試計劃

## 📋 測試目標
驗證 HomeKit 和 WebServer 資源競爭優化是否有效

## 🔬 測試項目

### 1. 記憶體監控測試
**目標**: 確認記憶體使用量穩定，無洩漏

**測試步驟**:
1. 重啟設備並記錄初始記憶體
2. 進行 HomeKit 配對
3. 配對期間同時訪問 WebServer 頁面
4. 觀察記憶體最小值和波動

**預期結果**:
- 初始記憶體: >150KB
- 配對期間最小記憶體: >80KB
- 記憶體不足警告: 應該很少或沒有

**監控指令**:
```bash
# 監控日誌中的記憶體資訊
pio device monitor | grep "記憶體"
```

### 2. HomeKit 配對性能測試
**目標**: 配對速度和成功率測試

**測試步驟**:
1. 刪除設備後重新配對
2. 配對期間同時訪問多個 WebServer 頁面
3. 記錄配對時間和成功率

**預期結果**:
- 配對時間: <30秒
- 配對成功率: >95%
- 配對期間 WebServer 仍可響應

### 3. WebServer 並發訪問測試
**目標**: WebServer 響應時間和穩定性

**測試腳本**:
```bash
# 並發訪問測試
for i in {1..10}; do
  echo "Test $i:"
  time curl -s "http://192.168.50.192:8080/" > /dev/null
  time curl -s "http://192.168.50.192:8080/status" > /dev/null
  time curl -s "http://192.168.50.192:8080/status-api" > /dev/null
  sleep 1
done
```

**預期結果**:
- 首頁響應時間: <2秒
- 狀態頁響應時間: <3秒
- API響應時間: <1秒
- 無超時錯誤

### 4. 長期穩定性測試
**目標**: 24小時運行穩定性

**測試步驟**:
1. 設備持續運行24小時
2. 每小時訪問一次 WebServer
3. 監控記憶體變化
4. 檢查設備重啟次數

**預期結果**:
- 零意外重啟
- 記憶體穩定 (波動<10%)
- WebServer 持續可訪問

### 5. 性能對比測試
**目標**: 優化前後性能對比

**測試項目**:
- HomeKit 響應時間 (優化前 vs 優化後)
- WebServer 頁面載入時間
- 記憶體使用效率
- CPU 使用率 (如可測量)

## 🎯 測試指標

### 記憶體使用量
- 初始: >150KB ✅
- 運行中最小: >80KB ✅  
- 配對期間: >100KB ✅

### 響應時間
- WebServer 首頁: <2s ✅
- HomeKit 指令: <1s ✅
- 配對完成: <30s ✅

### 錯誤率
- HTTP 錯誤: <1% ✅
- HomeKit 錯誤: <5% ✅
- 配對失敗率: <5% ✅

## 🔧 優化開關

如果性能不理想，可嘗試以下配置：

### 高性能模式
在 `platformio.ini` 中添加：
```ini
build_flags = 
    ${env.build_flags}
    -DHIGH_PERFORMANCE_WIFI
    -DPRODUCTION_BUILD
```

### 最小日誌模式  
在 `include/common/Debug.h` 中設定：
```cpp
#define DEBUG_LEVEL DEBUG_ERROR
```

### WebServer 關閉模式
如果 HomeKit 性能是最優先考量，可以暫時關閉 WebServer：
```cpp
// 在 main.cpp 中註解掉
// initializeMonitoring();
```

## 📊 測試報告格式

每次測試後記錄：
```
日期: YYYY-MM-DD
韌體版本: vX.X.X
測試環境: [描述]

記憶體使用:
- 初始: XXX KB
- 最小: XXX KB  
- 配對期間: XXX KB

響應時間:
- 首頁: X.X 秒
- 狀態頁: X.X 秒
- API: X.X 秒

錯誤統計:
- HTTP 錯誤: X 次
- HomeKit 錯誤: X 次
- 重啟次數: X 次

結論: [通過/失敗] + 說明
```

## 🚀 建議測試順序

1. **基礎功能測試**: 確認設備正常運行
2. **記憶體監控測試**: 建立基線數據
3. **配對性能測試**: 核心功能驗證
4. **並發訪問測試**: 資源競爭測試  
5. **長期穩定性測試**: 最終驗證

每個測試階段都應該記錄詳細數據，為後續優化提供依據。