## 背景
目前的記憶體優化層以 ESP32-C3 為核心調校，直接寫死了多組參數：閾值（25KB／18KB／15KB）、緩衝池大小（4×512B、3×1024B、2×2048B）以及 512B 的串流分塊。隨著專案引入新的建置型態（輕量除錯、S3 生產、模擬模式）與更大記憶體的硬體（ESP32-S3 搭配可選 PSRAM），這些固定數值不是浪費容量，就是導致 RAM 過度緊繃。此外，遙測資訊也無法辨識當下套用的假設，使得除錯更加困難。

### 現況盤點摘要
- 依 README.md：ESP32-C3 韌體的 RAM 使用率約 16.1%，換算約 140KB 可用堆積，更新頻率 6 秒一次，HomeKit 反應時間 <100ms。
- 開發者測試報告（TEST_PLAN.md、MemoryOptimization_Usage.md）顯示 Web 介面載入過程中可觀察 60–120KB 可用堆積波動，滿載時仍 >50KB。
- ESP32-S3 DevKitC-1 與 SuperMini 在無 PSRAM 情況下，官方規格提供約 512KB DRAM（部分為系統保留），實測剩餘堆積常超過 200KB。

### 平台環境對照
- `esp32-c3-supermini*`：ESP32-C3 (4MB flash / ~320KB RAM)，預設啟用 `ENABLE_REMOTE_DEBUG`，部分環境會定義 `ENABLE_LIGHTWEIGHT_DEBUG` 或 `PRODUCTION_BUILD`。
- `esp32-s3-*`：ESP32-S3 (16MB flash / ~512KB RAM)；生產環境帶 `PRODUCTION_BUILD`，OTA/USB 覆蓋遠端除錯需求。
- `*-lightweight` 或 `*-no-sim`：為記憶體緊縮模式，會停用模擬控制器或減少遙測頻寬。

## 目標
- 依可識別的硬體特徵（晶片、PSRAM、建置旗標）推導記憶體啟發式，讓每份韌體在啟動時就套用適合的配置。
- 支援覆寫配置檔（例如同一塊板子刷入不同模式的 OTA 韌體）而不必重新編譯閾值。
- 維持既有 API 穩定，同時將配置檔資訊透明供應給緩衝池、串流建構器與 Web UI。
- 在遙測中揭露選用的配置檔，強化可觀測性。

## 建議架構

### MemoryProfile 模型
- `MemoryProfile` 結構需涵蓋：
  - `name`／`hardware_tag`
  - 堆積閾值（`low`、`medium`、`high`），可為函式或常數
  - 各等級緩衝池數量與可選開關（例如輕量模式關閉大型緩衝池）
  - 串流分塊大小與最大渲染容量
  - 功能開關（是否啟用模擬模板、遠端除錯緩衝等）
- 提供協助方法計算衍生值，例如建議的緩衝大小列舉、同時可用的最大緩衝數量。

### 配置來源與選取
- `MemoryProfileFactory` 需檢查：
  - 編譯期巨集（`ESP32C3_SUPER_MINI`、`PRODUCTION_BUILD`、`ENABLE_LIGHTWEIGHT_DEBUG`、`ENABLE_REMOTE_DEBUG`）
  - 執行期訊號（`ESP.getChipModel()`、`ESP.getHeapSize()`、`ESP.getFreePsram()`）
- 回傳不可變的共享配置檔實例；未來若有需要，可支援讀取 flash 中的 JSON 以顯式覆寫。

### 整合點
1. **MemoryManager**
   - 將原本的固定閾值改由配置檔提供。
   - 追蹤配置檔版本；若發生切換（例如 OTA 後），需重置統計並重新計算策略。
   - 提供 `getActiveProfile()` 等方法供遙測使用。

2. **BufferPool**
   - 建構時帶入配置檔，依據設定建立緩衝池。
   - 配置變更時需安全地銷毀並重建緩衝池，避免轉換期間記憶體洩漏。

3. **StreamingResponseBuilder / WebPageGenerator**
   - 讀取配置檔提供的分塊與最大緩衝限制。
   - 視需要提供 `getChunkSize()` 等方法供儀表化使用。

4. **系統啟動**
   - 在 `initializeMemoryOptimization()` 內先解析配置檔，再建立 `MemoryManager`／`BufferPool`。
   - 啟動時記錄配置檔名稱、閾值與緩衝池數量。

5. **遙測**
   - `/api/memory/stats` 與 `/api/memory/detailed` 需要包含：
     - `profile.name`
     - 各項閾值與緩衝池數量
     - 分塊大小
     - 選擇原因（來源巨集或執行期檢測）
   - 遠端除錯需記錄配置檔切換紀錄。

### 永續化與覆寫（未來擴充）
- 預留設定鍵（如 `memory_profile_override`），以便後續需求可手動覆寫，目前預設關閉。

## 風險與因應
- **配置誤判**：新增健全性檢查（閾值需遞增、緩衝池數量不得為零），異常時回退到保守的 C3 配置並輸出警告。
- **執行期重設**：確保在沒有未釋放緩衝時才重新初始化池，否則排程至下一輪主迴圈處理。
- **遙測成本**：維持 JSON 擴充最小化，重用既有資料結構。

## 驗證策略
- 透過 `scripts/performance_test.py` 在調整前後量測堆積狀況。
- 觀察遠端除錯輸出的配置檔選取紀錄，涵蓋各 PlatformIO 環境。
- 確認 Web UI 反映了新的閾值與分塊設定。
