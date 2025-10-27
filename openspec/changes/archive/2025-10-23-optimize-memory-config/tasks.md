## 1. 基準盤點與分析
- [x] 1.1 針對 ESP32-C3 與 ESP32-S3（含 USB、量產環境）蒐集目前的堆積統計，取得真實可用記憶體區間與緩衝池使用狀況。
- [x] 1.2 對照各個 PlatformIO 環境與硬體特徵（可用堆積、PSRAM、除錯旗標），整理成日後配置檔的預設依據。

## 2. 記憶體配置檔實作
- [x] 2.1 新增 `MemoryProfile` 定義，包含各項閾值、分塊大小、緩衝池數量與功能開關，並提供工廠函式於執行期推導現行配置。
- [x] 2.2 重構 `MemoryOptimization::MemoryManager`，改從配置檔取得閾值／策略，並支援執行期切換。
- [x] 2.3 調整 `BufferPool` 與相關協助類別，使其依配置檔調整池大小，並在配置變更時安全地擴縮。
- [x] 2.4 讓 `StreamingResponseBuilder` 與 `WebPageGenerator` 能套用配置檔指定的分塊與上限設定。

## 3. 遙測與工具支援
- [x] 3.1 擴充 `/api/memory/*` 端點與遠端除錯輸出，回報所選配置檔及其限制。
- [x] 3.2 增加保護性測試或診斷腳本，確保各個 PlatformIO 環境（模擬、輕量除錯、生產）都選到預期的配置檔。

## 4. 文件與驗證
- [x] 4.1 更新 README、CLAUDE.md 與 `docs/MemoryOptimization_*` 文件，說明新的配置檔架構與調校方法。
- [ ] 4.2 為代表性環境（ESP32-C3 除錯版、ESP32-S3 生產版）執行 PlatformIO 建置，紀錄堆積與配置檔選取結果。
- [ ] 4.3 使用既有監控腳本（`scripts/quick_check.py`、`scripts/performance_test.py`）驗證效能沒有回退。
