## ADDED Requirements
### Requirement: 自適應記憶體配置檔
韌體 MUST 依硬體感知的記憶體配置檔推導閾值、緩衝池大小與串流分塊限制，而非使用硬編碼常數。

#### Scenario: ESP32-C3 Profile Selection
- **GIVEN** 韌體以 `esp32-c3-supermini`（無 PSRAM）環境建置
- **WHEN** 裝置完成 `initializeMemoryOptimization()`
- **THEN** 系統 MUST 套用 `c3-baseline`（或等價）配置檔，並記錄所選配置名稱與閾值。

#### Scenario: ESP32-S3 Production Profile
- **GIVEN** 韌體同時定義 `ESP32S3_SUPER_MINI` 與 `PRODUCTION_BUILD` 旗標
- **WHEN** 記憶體優化子系統啟動
- **THEN** 系統 MUST 套用高容量配置檔，啟用更大的緩衝池與分塊（並在遙測中呈現），且緊急閾值需維持在 80KB 可用堆積以上。

### Requirement: 記憶體配置檔遙測
記憶體監控 API MUST 揭露目前的配置檔與重要參數，方便維運人員驗證。

#### Scenario: Detailed Memory Endpoint
- **GIVEN** 裝置處於任一配置檔
- **WHEN** 用戶端請求 `GET /api/memory/detailed`
- **THEN** 回傳的 JSON MUST 包含 `profile.name`、閾值、緩衝池大小與串流分塊等欄位，且內容需與現行配置一致。
