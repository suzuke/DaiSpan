## 簡化核心功能架構

### 目標
- 以 ESP32-C3 SuperMini 為唯一支援板卡，提供穩定的 HomeKit 溫度/模式/風速控制。
- 保留必要的 Wi-Fi 配網與 HomeKit 配對頁面，確保部署流程完整。
- 將 OTA 更新設計為可選：預設關閉（以節省資源），需要時在 `platformio.ini` 或建置旗標中啟用。
- 移除遠端除錯（WebSocket/HTTP）、模擬模式、厚重的診斷頁面與 script 依賴，避免核心功能被拖慢。
- 建立輕量級除錯策略：透過序列埠 log（`DEBUG_INFO_PRINT` 等）搭配 `pio device monitor`，必要時可加入簡單的 `/health` 或 `/status` JSON 輸出。

### 預期刪除/調整的元件
| 類別 | 預設狀態 | 說明 |
|------|----------|------|
| 遠端除錯 (`ENABLE_REMOTE_DEBUG`, `/debug`, WebSocket 8081, HTTP 8082) | 移除 | 序列埠 log 取代；若未來需要遠端排錯，評估以外掛或選配形式回加。 |
| 模擬模式 (`MockThermostatController`, `/simulation*`) | 移除 | 生產版不需模擬控制；開發者可改用單元測試或離線模擬。 |
| 多板支援 (ESP32-S3 等) | 移除 | 只保留 ESP32-C3 SuperMini 環境與記憶體 profile。 |
| 大量診斷頁面/API (`/api/performance/*`, `/api/buffer/*` 等) | 視需求移除或關閉 | 若僅在重構期間需要，可改為選配旗標。同時調整測試腳本以免依賴此類端點。 |
| OTA | 可選（預設關閉） | 透過建置旗標/環境決定是否加入；必須清楚說明開啟時的記憶體影響。 |

### 保留的模組與頁面
- HomeKit 控制（含 S21 protocol、HomeSpan 配件）
- Wi-Fi 配網頁 (`/wifi`) 與配網流程（AP 模式）
- HomeKit 配置頁 (`/homekit`)
- 主頁資訊（提供基本狀態與序列埠除錯提示）

### 建議的架構調整
1. **平台設定**：`platformio.ini` 只留 `esp32-c3-supermini`，並提供 OTA 版位（可選）。
2. **記憶體配置檔**：`MemoryProfile` 中移除 S3 等 profile，只保留 C3 變體（baseline/optional-OTA）。
3. **條件編譯**：刪除遠端除錯與模擬相關的巨集，保留一個「MINIMAL_BUILD」旗標確保未來若要加選配功能可透過旗標管理。
4. **除錯策略**：維持 `DEBUG_*` 宏輸出至序列埠；必要時新增 `/api/health` 回傳單純 JSON（heap、firmware version）以便線上檢查。
5. **文件與腳本**：README、CLAUDE.md 重新描述最小功能；`quick_check.py` 僅需檢查主頁/Wi-Fi/HomeKit（無需模擬/OTA）；`performance_test.py` 改為可關閉某些測項或在缺少端點時自動跳過。

### 風險與對策
- **功能回退**：移除除錯/模擬後若需重新加入，應透過新 change 以選配方式落地，避免再度污染主線。
- **除錯困難**：序列埠 log 須維持足夠資訊（啟動、記憶體壓力、HomeKit 事件）。若遇到現場無法接入序列埠，可在未來規劃輕量 `/api/health` 供查詢。
- **測試腳本失效**：在刪除 API 前先盤點腳本使用情境，調整或精簡腳本以避免大量補救成本。
