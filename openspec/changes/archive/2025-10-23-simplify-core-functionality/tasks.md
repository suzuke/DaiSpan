## 1. 功能盤點與刪減
- [x] 1.1 清理程式與建置設定中與遠端除錯相關的模組（`OptimizedRemoteDebugger`, `RemoteDebugger`, `/debug` 等）與巨集。
- [x] 1.2 移除模擬模式（MockThermostatController、模擬頁面與相關 API），確保生產韌體僅包含真實 HomeKit 控制流程。
- [x] 1.3 將平台支援縮減為 ESP32-C3 SuperMini，刪除其他板卡環境、記憶體 profile 與對應文件敘述。

## 2. 必要功能保留與選項
- [x] 2.1 確認 HomeKit 控制（溫度/模式/風速）、Wi-Fi 配網、HomeKit 配對流程仍完整可用。
- [x] 2.2 將 OTA 更新改為可選功能：預設禁用，提供明確的啟用旗標與文件指引。
- [x] 2.3 補強序列埠 log 或其他輕量級方式，作為移除遠端除錯後的基本除錯手段。

## 3. 文件與測試調整
- [x] 3.1 更新 README、CLAUDE.md、MemoryOptimization 文件，反映最小功能架構與新的建置流程。
- [x] 3.2 精簡腳本（如 `quick_check.py`、`performance_test.py`）與測試文件，確保不再依賴被移除的功能。
- [x] 3.3 執行 `pio run -e esp32-c3-supermini`（必要時加 OTA 旗標測試）與 `scripts/quick_check.py` 驗證核心功能，紀錄結果。
