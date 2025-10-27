# DaiSpan - 進階大金 S21 HomeKit 橋接器

<div align="center">

![ESP32](https://img.shields.io/badge/ESP32-000000?style=flat&logo=espressif&logoColor=white)
![HomeKit](https://img.shields.io/badge/HomeKit-000000?style=flat&logo=apple&logoColor=white)
![PlatformIO](https://img.shields.io/badge/PlatformIO-FF7F00?style=flat&logo=platformio&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-blue.svg)

*專業級大金空調 HomeKit 橋接器，具備完整遠端調試功能*

[English Version](README.md) | [開發文檔](CLAUDE.md)

</div>

## 🌟 **核心功能**

### **HomeKit 整合**
- ✅ **完整 HomeKit 相容性** - 原生 iOS 家庭 app 支援
- 🌡️ **溫度控制** - 精確溫度監控與調節 (16°C - 30°C)
- 🔄 **運行模式** - 製熱、製冷、自動、送風、除濕模式
- 💨 **風速控制** - 多檔風速與自動模式
- ⚡ **即時更新** - 即時狀態同步
- 📱 **Siri 整合** - 語音控制支援

### **進階遠端調試** 🛠️
- 🌐 **WebSocket 遠端調試** - 無需 USB 連接的即時監控
- 📊 **即時串口日誌** - 等同 `pio device monitor` 但可遠端存取
- 🔍 **系統診斷** - 全面的健康檢查與狀態監控
- 📈 **效能監控** - 記憶體使用、WiFi 信號、系統運行時間追蹤
- 🎯 **HomeKit 操作追蹤** - 即時記錄所有 HomeKit 互動
- 💻 **網頁調試界面** - 專業調試儀表板，任何裝置都可存取

### **協議與硬體支援**
- 🔌 **多種 ESP32 變體** - ESP32-S3、ESP32-C3 SuperMini 支援
- 📡 **S21 協議版本** - 完整支援 1.0、2.0 與 3.xx 版本
- 🔄 **自動協議偵測** - 自動版本偵測與最佳化
- 🏗️ **模組化架構** - 可擴展設計，支援未來其他空調品牌
- ⚙️ **OTA 更新** - 無線韌體更新

### **網頁界面與管理**
- 🌐 **完整網頁儀表板** - 狀態監控與設定
- 🛜 **WiFi 管理** - 簡易設定與認證管理
- 🔧 **設定界面** - HomeKit 設定與裝置管理
- 🇹🇼 **繁體中文支援** - 完整繁體中文界面
- 📊 **即時監控** - 即時系統狀態與效能指標

## 🆕 **最新架構更新**

- 🔄 **HomeKit 指令佇列化**：新增排程式命令佇列，所有模式 / 溫度變更都會等待設備確認，確保 HomeKit 與實體空調狀態一致。
- 🚦 **記憶體壓力等級**：主頁面與 `/api/health` 會顯示「正常 / 緊張 / 嚴重」三段式記憶體狀態，方便快速判斷是否需要降載。
- ⏱️ **排程器負載指標**：即時顯示最近一次排程所執行的任務數與耗時（μs / ms），協助診斷主迴圈瓶頸。
- 🧹 **精簡核心架構**：移除 legacy 事件總線與服務容器，主程序僅保留 HomeKit + Wi-Fi + OTA 必要模組，降低記憶體占用。

## 🛠️ **硬體需求**

### **支援的 ESP32 開發板**
| 開發板 | 狀態 | RX 腳位 | TX 腳位 | Flash 大小 | 備註 |
|-------|------|---------|---------|------------|------|
| **ESP32-S3 DevKitC-1** | ✅ 主要 | 14 | 13 | 16MB | 建議使用 |
| **ESP32-C3 SuperMini** | ✅ 已測試 | 4 | 3 | 4MB | 精巧選擇 |
| **ESP32-S3 SuperMini** | ✅ 支援 | 13 | 12 | 16MB | 替代方案 |

### **額外硬體**
- 🔌 **TTL 轉 S21 轉接器** (3.3V 電平)
- 🏠 **大金空調** 具備 S21 接口
- 📶 **穩定 WiFi 連線** (2.4GHz)

## 🚀 **快速開始**

### **1. 安裝**

```bash
# 複製儲存庫
git clone https://github.com/your-username/DaiSpan.git
cd DaiSpan

# 安裝 PlatformIO (如果尚未安裝)
pip install platformio

# 建置韌體
pio run

# 上傳到 ESP32 (選擇您的開發板)
pio run -e esp32-s3-usb -t upload        # ESP32-S3 透過 USB
pio run -e esp32-c3-supermini-usb -t upload # ESP32-C3 透過 USB
```

### **2. 初始設定**

1. **開啟** ESP32 裝置電源
2. **連接** WiFi 網路 "DaiSpan-Config"
3. **瀏覽** 網頁界面 (通常是 `192.168.4.1`)
4. **設定** WiFi 認證與 HomeKit 設定
5. **新增** 到 iOS 家庭 app，配對碼：`11122333`

### **3. 進階開發**

```bash
# 多種建置環境
pio run -e esp32-s3-usb -t upload          # USB 上傳
pio run -e esp32-s3-ota -t upload          # OTA 上傳 (IP: 在 platformio.ini 中設定)

# 監控與調試
pio device monitor                          # 本地串口監控
# 或使用遠端調試 http://device-ip:8080/debug

# 測試與驗證
python3 scripts/quick_check.py [device_ip] # 快速健康檢查
python3 scripts/long_term_test.py 192.168.4.1 24 5  # 24小時穩定性測試
```

## 🌐 **遠端調試系統**

DaiSpan 的一大亮點是完整的遠端調試功能：

### **存取遠端調試器**
```
http://your-device-ip:8080/debug
```

### **功能特色**
- 📡 **即時串口日誌** - 遠端檢視所有調試輸出
- 📊 **系統狀態監控** - 記憶體、WiFi、HomeKit 狀態
- 🔍 **即時診斷** - 系統健康檢查
- 📈 **效能指標** - 即時系統效能數據
- 🏠 **HomeKit 操作追蹤** - 監控所有 HomeKit 互動
- 🎯 **多客戶端支援** - 多個瀏覽器可同時連接

### **WebSocket 整合**
調試系統使用 WebSocket 進行即時通訊：
- **WebSocket 伺服器**: `ws://device-ip:8081`
- **協議**: 基於 JSON 的命令/回應系統
- **命令**: `get_status`、`diagnostics`、`get_history`

## 🏗️ **架構**

DaiSpan 採用清晰的模組化架構：

```
┌─ 裝置層 (HomeKit 整合)
├─ 控制器層 (業務邏輯)  
├─ 協議層 (S21 通訊)
└─ 共用層 (工具與設定)
```

### **核心元件**
- 🏭 **協議工廠** - 可擴展的空調協議支援
- 🔄 **轉接器模式** - 清晰的協議抽象
- 🎯 **依賴注入** - 模組化、可測試設計
- 🛡️ **錯誤恢復** - 強健的錯誤處理與恢復

## 📊 **效能與記憶體**

### **目前指標 (ESP32-C3)**
- **Flash 使用**: 91.3% (1.53MB/1.69MB)
- **RAM 使用**: 16.1% (約140KB可用)
- **更新頻率**: 6秒狀態輪詢
- **回應時間**: HomeKit 操作 <100ms

### **記憶體管理**
- ✅ **最佳化分割區** - 自訂分割區表支援 OTA
- ♻️ **動態記憶體** - 高效記憶體配置
- 📈 **監控** - 即時記憶體使用追蹤

## 🧪 **測試與品質保證**

### **自動化測試腳本**
```bash
# 快速系統驗證
python3 scripts/quick_check.py [device_ip]

# 長期穩定性測試
python3 scripts/long_term_test.py [device_ip] [小時] [間隔分鐘]

# 資源監控
./scripts/resource_monitor.sh [device_ip] [間隔秒數]
```

### **測試策略**
- 🔬 **單元測試** - 協議抽象支援獨立測試
- 🏃 **整合測試** - 網頁界面手動測試
- 📊 **效能測試** - 長期穩定性驗證
- 🔄 **模擬模式** - 開發用模擬控制器

## 🛠️ **設定**

### **硬體腳位設定**
根據 `include/common/Config.h` 中的開發板選擇自動設定：

```cpp
// ESP32-C3 SuperMini
#define S21_RX_PIN 4
#define S21_TX_PIN 3

// ESP32-S3 變體
#define S21_RX_PIN 13  // DevKitC-1 為 14
#define S21_TX_PIN 12  // DevKitC-1 為 13
```

### **運行模式**
1. **正式模式** - 透過 S21 協議與真實空調通訊
2. **模擬模式** - 用於測試與開發的模擬實作

## 🤝 **貢獻**

歡迎貢獻！請參考我們的貢獻指南：

1. 🍴 **Fork** 儲存庫
2. 🌿 **建立** 功能分支
3. ✅ **徹底測試** 您的更改
4. 📝 **記錄** 新功能
5. 🔄 **提交** pull request

## 📚 **文件**

- 📖 **[CLAUDE.md](CLAUDE.md)** - 全面開發指南
- 🏗️ **[MAIN_LOOP_REFACTORING.md](MAIN_LOOP_REFACTORING.md)** - 架構決策
- 🧪 **[scripts/README.md](scripts/README.md)** - 測試工具文件

## 🐛 **故障排除**

### **常見問題**
- **HomeKit 無回應**: 檢查 `/debug` 的遠端調試日誌
- **WiFi 連線問題**: 確認 2.4GHz 網路相容性
- **S21 通訊錯誤**: 確認腳位連接與協議設定
- **記憶體問題**: 透過遠端調試界面監控使用情況

### **調試資源**
- 🌐 **遠端調試界面**: `http://device-ip:8080/debug`
- 📡 **WebSocket 日誌**: `ws://device-ip:8081`
- 📊 **系統診斷**: 內建健康檢查命令

## 📄 **授權條款**

MIT 授權 - 詳見 [LICENSE](LICENSE)。

本專案包含以下專案的程式碼與概念：
- **HomeSpan** (Copyright © 2020-2024 Gregg E. Berman)
- **ESP32-Faikin** (Copyright © 2022 Adrian Kennard)

## 🙏 **致謝**

特別感謝：
- 🏠 **HomeSpan 專案** - 優秀的 HomeKit 函式庫
- 🌊 **ESP32-Faikin** - S21 協議基礎
- 🤖 **Claude Code** - 開發協助與遠端調試系統設計

---

<div align="center">

**為智慧家庭社群用❤️製作**

*如需支援，請使用遠端調試工具或查看文件*

</div>
