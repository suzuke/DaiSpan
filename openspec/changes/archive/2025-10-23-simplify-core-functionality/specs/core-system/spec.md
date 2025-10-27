## ADDED Requirements
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
