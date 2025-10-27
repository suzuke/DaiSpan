## ADDED Requirements
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
