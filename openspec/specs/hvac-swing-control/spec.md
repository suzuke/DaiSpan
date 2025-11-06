# hvac-swing-control Specification

## Purpose
TBD - created by archiving change add-hvac-swing-control. Update Purpose after archive.
## Requirements
### Requirement: HomeKit Swing Control Exposure
Daikin S21 裝置在協議層回報支援擺動時，HomeKit 介面 MUST 提供各自的水平與垂直擺動控制，並在不支援時自動隱藏或鎖定。

#### Scenario: HomeKit Shows Swing Toggles
- **GIVEN** 韌體偵測到協議支援垂直擺動
- **WHEN** 使用者開啟 Apple Home App
- **THEN** HomeKit 服務 MUST 顯示可切換的垂直擺動控制
- **AND** 若同時支援水平擺動，水平控制 MUST 一併顯示。

#### Scenario: Unsupported Swing Hidden
- **GIVEN** 韌體偵測到協議無垂直或水平擺動能力
- **WHEN** 建置或重新啟動 HomeKit 配件
- **THEN** 對應的擺動控制 MUST 不顯示於 HomeKit，或以唯讀狀態呈現，不允許用戶互動。

### Requirement: HomeKit Swing Angle Selection
當協議提供可選角度列表時，HomeKit MUST 允許使用者設定垂直與水平固定角度，並顯示當前角度。

#### Scenario: Angle Selection With Capability
- **GIVEN** 韌體取得垂直擺動的可用角度清單
- **WHEN** 使用者從 HomeKit 選擇特定角度
- **THEN** 韌體 MUST 透過協議下發對應的固定角度指令
- **AND** HomeKit MUST 更新為該角度的狀態。

#### Scenario: Angle Control Hidden Without Capability
- **GIVEN** 協議未提供固定角度能力
- **WHEN** 使用者造訪 HomeKit 裝置頁面
- **THEN** 垂直與水平角度特性 MUST 隱藏或鎖定在預設值，避免誤導使用者。

### Requirement: Swing Command Confirmation
擺動或角度命令 MUST 在協議層成功確認後才更新 HomeKit 狀態，失敗時需回退並回報錯誤。

#### Scenario: Toggle Confirmation
- **GIVEN** 使用者在 HomeKit 切換垂直擺動
- **WHEN** 韌體送出對應的擺動指令
- **THEN** 系統 MUST 在 2 秒內收到成功回覆才更新 HomeKit 狀態
- **AND** 若逾時或失敗 MUST 回復原狀並記錄錯誤。

#### Scenario: Angle Confirmation
- **GIVEN** 使用者設定新的水平固定角度
- **WHEN** 協議回覆錯誤或逾時
- **THEN** HomeKit MUST 回復先前角度並提示錯誤，避免顯示錯誤狀態。

