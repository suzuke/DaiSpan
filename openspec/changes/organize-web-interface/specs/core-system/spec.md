## ADDED Requirements
### Requirement: Grouped Web Interface Panels
核心 web 介面 MUST 依「狀態資訊」與「操作入口」分組，將功能相近的資訊或表單放在同一區塊，避免使用者在不同頁面來回尋找設定。

#### Scenario: Home Dashboard Panels
- **WHEN** 使用者造訪 `/` 首頁
- **THEN** 頁面 MUST 提供一個統一的「狀態面板」，同時呈現系統/網路/HomeKit 主要指標
- **AND** 所有設定或工具連結 MUST 被集中在單一「操作面板」中（例如 Wi-Fi 設定、HomeKit 設定、OTA），以卡片或列表形式聚合。

#### Scenario: Configuration Pages Share Layout
- **WHEN** 使用者造訪 `/wifi` 或 `/homekit` 等設定頁
- **THEN** 頁面 MUST 先在頂部顯示與該功能相關的狀態摘要（例如目前連線、配對狀態）
- **AND** 相關表單與動作（掃描、儲存、重置等） MUST 與該狀態摘要位於同一內容區塊內，採用一致的版面骨架（標題、狀態卡、操作卡），以減少跳離頁面的需求。
