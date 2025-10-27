# core-system Specification

## Purpose
TBD - created by archiving change simplify-core-functionality. Update Purpose after archive.
## Requirements
### Requirement: Minimal HomeKit Bridge Feature Set
韌體 MUST 僅保留 HomeKit 溫度/模式/風速控制、Wi-Fi 配網、HomeKit 配對，並以 ESP32-C3 SuperMini 為唯一支援板卡；模擬模式與遠端除錯 MUST NOT 隨生產版一起提供。

#### Scenario: Production Firmware Scope
- **GIVEN** 韌體以 `esp32-c3-supermini` 環境建置
- **WHEN** 裝置啟動並提供 Web 介面
- **THEN** 使用者 MUST 能完成 Wi-Fi 配網與 HomeKit 配對
- **AND** HomeKit 控制功能 MUST 正常運作（溫度、模式、風速）
- **AND** 模擬頁面、遠端除錯（WebSocket/HTTP） MUST NOT 在生產版出現。

### Requirement: Optional OTA Support
OTA 更新能力 MUST 為可選功能：預設關閉，僅在明確啟用旗標時編譯進韌體，並需在文件中說明記憶體與部署影響。

#### Scenario: OTA Disabled By Default
- **GIVEN** 預設建置設定（未開 OTA 旗標）
- **WHEN** 裝置啟動
- **THEN** `/ota` 相關頁面或服務 MUST NOT 提供，且韌體不得連結 OTA 相關模組。

#### Scenario: OTA Enabled Explicitly
- **GIVEN** 開發者在 PlatformIO 環境或建置旗標中顯式啟用 OTA
- **WHEN** 韌體建置完成並燒錄
- **THEN** OTA 服務 MUST 正常提供更新資訊與上傳入口
- **AND** 文件 MUST 指出啟用 OTA 後的記憶體需求與注意事項。
*** End Patch

### Requirement: HomeKit-S21 Consistent Synchronization
HomeKit 指令執行後 MUST 於有限時間內（建議 2 秒）與實際 S21 設備狀態完成確認，若失敗 MUST 回報錯誤並回滾 HomeKit 特性，避免假成功狀態。

#### Scenario: Command Confirmation
- **GIVEN** 使用者在 HomeKit App 對裝置發出控制（如改變模式或溫度）
- **WHEN** 系統送出對應 S21 指令
- **THEN** 系統 MUST 在 2 秒內收到成功回報並同步更新 HomeKit 狀態；若逾時或失敗 MUST 將特性還原並輸出警示記錄。

### Requirement: Main Loop Scheduling Efficiency
主迴圈 MUST 以具優先權與節流的排程模型執行，確保高優先任務不被長時間阻塞，並於 Heap 緊張時降載低優先任務。

#### Scenario: Scheduler Behavior
- **GIVEN** 系統同時處理 HomeKit 指令、S21 輪詢與 Web 請求
- **WHEN** 任務排入主迴圈
- **THEN** HomeKit/S21 任務 MUST 優先執行且不被 Web 任務阻塞；若 Heap 低於安全閾值，Web/診斷任務 MUST 進入降載模式（延遲或跳過）。

