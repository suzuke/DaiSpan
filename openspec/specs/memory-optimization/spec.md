# memory-optimization Specification

## Purpose
TBD - created by archiving change optimize-memory-config. Update Purpose after archive.
## Requirements
### Requirement: 自適應記憶體配置檔
韌體 MUST 依硬體感知的記憶體配置檔推導閾值、緩衝池大小與串流分塊限制，而非使用硬編碼常數。

#### Scenario: Minimal ESP32-C3 Profile
- **GIVEN** 韌體以 `esp32-c3-supermini`（預設無 OTA）環境建置
- **WHEN** 裝置完成 `initializeMemoryOptimization()`
- **THEN** 系統 MUST 套用預設的 C3 最小化配置檔並記錄所選配置名稱、閾值與緩衝池設定。

#### Scenario: OTA-Enabled ESP32-C3 Profile
- **GIVEN** 建置時定義 `ENABLE_OTA_UPDATE` 旗標
- **WHEN** 記憶體優化子系統啟動
- **THEN** 系統 MUST 套用 OTA 專用配置檔（調整緩衝池與分塊大小），並在遙測中呈現 OTA profile 名稱與限制。

### Requirement: 記憶體配置檔遙測
記憶體監控 API MUST 揭露目前的配置檔與重要參數，方便維運人員驗證。

#### Scenario: Detailed Memory Endpoint
- **GIVEN** 裝置處於任一配置檔
- **WHEN** 用戶端請求 `GET /api/memory/detailed`
- **THEN** 回傳的 JSON MUST 包含 `profile.name`、閾值、緩衝池大小與串流分塊等欄位，且內容需與現行配置一致。

