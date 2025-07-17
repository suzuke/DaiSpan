# DaiSpan 記憶體優化使用指南

## 📋 概述

此文檔介紹DaiSpan專案新添加的記憶體優化功能和性能監控API端點的使用方法。

## 🚀 新增功能

### 1. 記憶體優化組件
- **StreamingResponseBuilder**: 流式HTTP響應，512字節分塊傳輸
- **BufferPool**: 三級緩衝區池 (512B/1024B/2048B)
- **MemoryManager**: 四級內存壓力監控和自適應策略
- **WebPageGenerator**: 統一的頁面生成器

### 2. 監控API端點

#### 基本記憶體統計
```bash
GET /api/memory/stats
```
返回基本的記憶體優化狀態信息。

#### 詳細記憶體分析
```bash
GET /api/memory/detailed
```
返回詳細的記憶體分析數據，包括：
- 堆記憶體狀態
- 記憶體壓力等級
- 渲染策略信息
- 系統統計

#### 緩衝區池統計
```bash
GET /api/buffer/stats
```
返回緩衝區池的使用統計信息。

#### 系統儀表板
```bash
GET /api/monitor/dashboard
```
返回完整的系統狀態信息：
- 系統運行時間
- 記憶體狀態
- WiFi連接狀態
- HomeKit狀態
- 記憶體優化狀態

### 3. 性能測試API端點

#### 基本性能測試
```bash
GET /api/performance/test
```
執行基本性能測試，包括：
- 記憶體分配性能
- 流式響應性能
- JSON生成性能

#### 負載測試
```bash
GET /api/performance/load?iterations=20&delay=100
```
參數：
- `iterations`: 迭代次數 (1-100)
- `delay`: 每次迭代延遲 (50-1000ms)

#### 基準測試
```bash
GET /api/performance/benchmark
```
對比傳統方法和優化方法的性能差異。

## 🔧 使用方法

### 1. 部署韌體

```bash
# 編譯韌體
pio run -e esp32-c3-supermini-usb

# 上傳韌體 (USB)
pio run -e esp32-c3-supermini-usb -t upload

# 上傳韌體 (OTA)
pio run -e esp32-c3-supermini-ota -t upload
```

### 2. 使用測試腳本

#### 快速測試
```bash
python3 scripts/simple_test.py 192.168.4.1
```

#### 詳細測試
```bash
python3 scripts/simple_test.py 192.168.4.1 --test detailed
```

#### JSON輸出
```bash
python3 scripts/simple_test.py 192.168.4.1 --json
```

#### 完整性能測試
```bash
python3 scripts/performance_test.py 192.168.4.1 --output report.txt
```

### 3. 手動API測試

#### 使用curl測試
```bash
# 基本記憶體狀態
curl http://192.168.4.1:8080/api/memory/stats | jq

# 詳細記憶體分析
curl http://192.168.4.1:8080/api/memory/detailed | jq

# 性能測試
curl http://192.168.4.1:8080/api/performance/test | jq

# 負載測試
curl "http://192.168.4.1:8080/api/performance/load?iterations=10&delay=200" | jq

# 系統儀表板
curl http://192.168.4.1:8080/api/monitor/dashboard | jq
```

#### 使用瀏覽器
直接在瀏覽器中訪問：
- `http://192.168.4.1:8080/api/monitor/dashboard`
- `http://192.168.4.1:8080/api/memory/detailed`
- `http://192.168.4.1:8080/api/performance/benchmark`

## 📊 性能指標

### 預期改善效果
- **記憶體峰值使用**: 降低 84-92% (從6KB到512-1024B)
- **動態分配次數**: 降低 100% (從每次3-5次到0次)
- **響應時間**: 提升 50-80%
- **並發支持**: 提升 300% (從1-2用戶到5-8用戶)

### 監控指標含義

#### 記憶體壓力等級
- **LOW**: >80KB 可用記憶體
- **MEDIUM**: 50-80KB 可用記憶體
- **HIGH**: 30-50KB 可用記憶體
- **CRITICAL**: <30KB 可用記憶體

#### 渲染策略
- **FULL_FEATURED**: 完整功能模式
- **OPTIMIZED**: 優化模式
- **MINIMAL**: 最小化模式
- **EMERGENCY**: 緊急模式

## 🛠️ 故障排除

### 常見問題

#### 1. API端點無法訪問
**症狀**: 404 Not Found 或連接超時

**解決方案**:
```bash
# 檢查設備IP
ping 192.168.4.1

# 檢查WebServer狀態
curl -I http://192.168.4.1:8080/

# 檢查串口輸出
pio device monitor
```

#### 2. 記憶體優化未啟用
**症狀**: `/api/memory/detailed` 返回 "Memory optimization not initialized"

**解決方案**:
1. 確認韌體編譯時包含了記憶體優化代碼
2. 檢查設備是否有足夠記憶體啟動優化功能
3. 查看串口日誌確認初始化狀態

#### 3. 性能測試失敗
**症狀**: 測試腳本報告連接錯誤或API錯誤

**解決方案**:
1. 確認設備IP地址正確
2. 檢查防火牆設置
3. 確認設備不在HomeKit配對模式
4. 嘗試降低測試參數（迭代次數、並發數等）

### 調試技巧

#### 查看記憶體使用情況
```bash
# 監控記憶體變化
while true; do
  curl -s http://192.168.4.1:8080/api/memory/stats | jq '.freeHeap'
  sleep 2
done
```

#### 查看系統日誌
```bash
# 串口監控
pio device monitor --baud 115200

# 過濾記憶體相關日誌
pio device monitor | grep -i memory
```

#### 記憶體壓力測試
```bash
# 高頻率訪問測試
for i in {1..50}; do
  curl -s http://192.168.4.1:8080/api/memory/detailed > /dev/null
  echo "Request $i completed"
  sleep 0.1
done
```

## 📈 性能調優建議

### 1. 記憶體優化配置
- 監控記憶體壓力等級，保持在 LOW 或 MEDIUM
- 如果經常達到 HIGH 或 CRITICAL，考慮減少功能負載
- 定期檢查記憶體碎片化率，保持在15%以下

### 2. API使用最佳實踐
- 避免高頻率調用性能測試API
- 使用 `/api/monitor/dashboard` 進行定期監控
- 負載測試時設置合理的迭代次數和延遲

### 3. 系統監控策略
- 設置定期監控腳本，每5分鐘檢查一次系統狀態
- 記錄性能基線數據，用於比較分析
- 在系統升級前後進行性能對比測試

## 📝 日誌分析

### 重要日誌訊息
```
[Main] 記憶體優化組件已初始化
[Main] WebServer監控功能已啟動
[API] 記憶體優化狀態查詢完成
[API] 性能測試完成: XX ms, 記憶體變化: XX bytes
[API] 負載測試完成: XX 次迭代, 總時間: XX ms
```

### 錯誤訊息處理
```
記憶體不足，跳過WebServer啟動 → 重啟設備或減少功能
Memory optimization not initialized → 檢查初始化代碼
API請求失敗 → 檢查網路連接和設備狀態
```

## 🔄 持續監控

### 建議監控頻率
- **記憶體狀態**: 每30秒
- **系統儀表板**: 每分鐘
- **性能基準**: 每小時
- **負載測試**: 每日一次

### 監控腳本範例
```bash
#!/bin/bash
# 持續監控腳本
DEVICE_IP="192.168.4.1"

while true; do
  echo "$(date): 檢查系統狀態..."
  
  # 獲取基本狀態
  curl -s "http://$DEVICE_IP:8080/api/monitor/dashboard" | \
    jq '.system.freeHeap, .memoryOptimization.pressure'
  
  sleep 60
done
```

## 📞 技術支持

如遇到問題，請提供以下信息：
1. 設備型號和韌體版本
2. 相關API端點的完整響應
3. 串口日誌輸出
4. 測試腳本的完整輸出
5. 網路環境描述

更多技術細節請參考：
- [設計文檔](MemoryOptimization_Design.md)
- [實現範例](../examples/MemoryOptimization_Example.cpp)
- [核心代碼](../include/common/MemoryOptimization.h)