# DaiSpan - 大金 S21 HomeKit 橋接器

基於 ESP32 和 HomeSpan 的大金空調 S21 接口 HomeKit 橋接器。

本專案基於以下開源專案：
- [HomeSpan](https://github.com/HomeSpan/HomeSpan) - ESP32 的 HomeKit 函式庫
- [ESP32-Faikin](https://github.com/revk/ESP32-Faikin) - S21 協議實現

## 版權聲明

Copyright (c) 2024 DaiSpan

本專案包含以下專案的程式碼和概念：
- HomeSpan (Copyright © 2020-2024 Gregg E. Berman)
- ESP32-Faikin (Copyright © 2022 Adrian Kennard)

依據 MIT 授權條款授權，這表示：
- 您可以自由使用、修改和分發本軟體
- 您必須包含原始版權聲明和授權條款
- 本軟體按「現狀」提供，不提供任何形式的保證

## 功能特點

- 支援大金 S21 協議版本 1.0、2.0 和 3.xx
- 自動檢測協議版本
- 溫度監控和控制
- 運行模式控制（製冷/製熱/自動/送風/除濕）
- 風速控制
- 電源開關控制
- 即時狀態更新
- 網頁式日誌界面
- OTA 韌體更新

## 硬體需求

- ESP32 開發板
- - ESP32-S3-DevKitC-1（推薦使用）
- TTL 轉 S21 轉接器
- - TTL 轉 S21 轉接器（3.3V 電平）
- 具有 S21 接口的大金空調

## 接腳配置

- S21 RX：GPIO16（RX2）
- S21 TX：GPIO17（TX2）
- 狀態指示燈：GPIO2

## 安裝方法

1. 安裝 PlatformIO
2. 克隆此儲存庫
3. 在 `src/main.cpp` 中配置 WiFi 認證
4. 編譯並上傳至 ESP32

## 使用方法

1. 開啟設備電源
2. 等待 WiFi 連接（狀態指示燈會顯示）
3. 在 Home 應用程式中使用配對碼添加配件
4. 預設配對碼：11122333

## 協議支援

- 基本命令（所有版本）：
  - 狀態查詢/設置（F1/D1）
  - 溫度查詢（RH）
- 2.0 版本以上：
  - 擴展狀態（F8/D8）
  - 自動模式
  - 除濕模式
  - 送風模式
- 3.xx 本：
  - 功能檢測（F2）
  - 版本查詢（FY）
  - 額外設置（FK/FU）

## 開發相關

- 使用 PlatformIO 構建
- 使用 HomeSpan 庫實現 HomeKit 整合
- 模組化架構便於擴展

## 授權條款

MIT 授權

## 致謝

- HomeSpan 專案
- 大金 S21 協議文檔 