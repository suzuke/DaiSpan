## Context
- 現行主迴圈 (`SystemManager::processMainLoop`) 同時處理 HomeKit、S21 輪詢、Wi-Fi、WebServer、OTA 等任務，缺乏明確優先權與確認機制。
- HomeKit 指令送出後直接呼叫 S21 控制器，但缺少確認與狀態同步，導致 App 顯示和實際硬體不同步。
- Web 頁面生成仍依賴即時拼接，當 Heap 緊張或同時有多個 HTTP 請求時會拉長回應時間。

## Goals
1. 確保「HomeKit → S21 → 狀態回報」流程可驗證且在 2 秒內完成，並在失敗時回滾或提示。
2. 主迴圈任務具明確優先權與節流策略，避免長時間阻塞或過度輪詢。
3. Web/UI 任務保持在低優先序，並使用快取/增量更新以減少 Heap 消耗。
4. 架構簡化，移除未使用的服務容器或事件機制，保留最小可維護單元。

## Proposed Architecture
### 1. 任務與排程
### Proposed Scheduler Model
- 新增 `core/Scheduler`：維護三個佇列 (HIGH/MEDIUM/LOW)，每個 task 含下一次執行時間與執行函式。
- HIGH：HomeKit 指令執行、S21 確認；MEDIUM：週期性 S21 輪詢；LOW：Web 處理、Wi-Fi 功能。
- Scheduler 持有 heap 閾值：當 `ESP.getFreeHeap()` 低於 45KB 時，LOW 任務延後；低於 35KB 時暫停 LOW 並延長 MEDIUM 間隔。
- `SystemManager::processMainLoop()` 改為：先 `scheduler.runDueTasks()`，再手動觸發必要的 HomeSpan poll。
- HomeKit 指令放入 HIGH 佇列，由 `processPendingCommand()` 執行並立刻發起確認（最多重試 1 次，超時 2 秒）。

- 建立 Scheduler：區分 `HIGH` (HomeKit 指令、S21 ACK)、`MEDIUM` (定期狀態輪詢)、`LOW` (Web/診斷)。
- HomeKit 指令觸發後插入 `HIGH` 任務，等待 S21 回應或超時，再更新狀態並送出事件。
- `LOW` 任務僅在 Heap > safety threshold 時執行；若壓力大則延遲。

### 2. 同步確認與回滾
- 引入 Command Context：記錄指令型別、目標值、開始時間、重試次數。
- S21 執行完畢需回傳成功/失敗；若超時則重試一次或回滾 HomeKit 特性至舊值並輸出提示。
- 定期對照 S21 報文與 HomeKit 狀態，發現差異即觸發自動同步任務。

### 3. Web/UI 優化
- 頁面內容採模板快取：狀態資料與 HTML 分離，使用小型 placeholder 更新。
- `/api/memory/*` 與儀表板提供輕量 JSON（避免大字串），必要時提供分頁或節流。

### 4. 架構清理
- 評估 `core/EventSystem` 與 `ServiceContainer` 是否仍有用；若無則移除或整合進 Scheduler。
- 確保 WiFiManager 與 HomeKit 控制器不再互相依賴未使用的 helper。

### Current Observations (Task 1)
- `SystemManager::processMainLoop()` 僅使用 loopCounter 與 fastLoopDivider 控制頻率；HomeKit (`homeSpan.poll()`) 每次主迴圈都執行，S21 狀態由 `ThermostatDevice::loop()` 每次呼叫且無節流。
- `ThermostatController::update()` 以 6 秒 `UPDATE_INTERVAL` 查詢 S21；HomeKit 指令 (`ThermostatDevice::update`) 直接呼叫 `controller.set*`，即使協議拒絕也回傳 true，需靠後續輪詢修正。
- SystemManager 同時處理 Wi-Fi 重連、WebServer `handleClient()`、配對偵測、心跳輸出等，缺乏任務優先權與調度。
- 記憶體緊縮時僅調整 Web 任務頻率；`ThermostatDevice::loop()` 仍強制通知 HomeKit，可能造成多餘事件並拖慢回應。
- 缺乏指令確認／回滾機制：HomeKit update() 永遠回 true；S21 失敗僅記 log，導致 App 與實體狀態不同步。

### Root Cause Summary (Task 1.2)
- HomeKit 指令直接呼叫 `ThermostatController::set*`，協議失敗只寫入 log；HomeKit 仍保持新值直到下一次輪詢覆蓋。
- S21 狀態輪詢間隔為 6 秒，且在錯誤恢復模式下可能暫停查詢，導致回滾延遲或完全缺失。
- `ThermostatDevice::loop()` 每次都強制 `setVal()/timeVal()`，即使設備狀態未變化也推送通知，增加事件量但無法保證一致性。
- 缺少集中式任務調度：Wi-Fi/Web 任務與 HomeKit/S21 共用同一主迴圈，當 Web 任務耗時或記憶體吃緊時會延後狀態更新。
- 因此需要新的同步協定：指令送出→等待 ACK → 成功才更新 HomeKit；若失敗需回滾並提示，並記錄重試次數與超時。

### 5. 監控與診斷
- 在 Scheduler 中紀錄任務執行時間、排隊長度、Heap snapshot，提供 `/api/system/metrics` 輸出。
- 將測試腳本更新為檢查 HomeKit 指令 → 實體狀態回報時間、頁面回應時間。

## Risks & Mitigations
- **S21 控制流程複雜**：須確認現有控制器 API 支援同步結果；必要時包裝。
- **多任務調度**：需小心避免 blocking；先以 Cooperatively Scheduler 為主。
- **Web 快取一致性**：當狀態更新時需 invalid cache；設計簡單 invalidation 規則。
