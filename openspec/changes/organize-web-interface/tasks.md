## 1. 版面盤點與設計
- [ ] 1.1 檢視 `generateUnifiedMainPage()`、`/wifi`、`/homekit` 產生流程並標記目前資訊/操作分布。
- [ ] 1.2 擬定「狀態面板 + 操作面板」結構草圖，列出每個區塊的資料來源與 UI 元素。

## 2. 首頁重構
- [ ] 2.1 更新首頁 HTML/CSS：建立共用的狀態區塊（系統、網路、HomeKit）與操作區塊（Wi-Fi、HomeKit、OTA…）。
- [ ] 2.2 確保 StreamingResponseBuilder/記憶體策略不退化，並驗證在記憶體緊縮模式下仍可顯示主要狀態。

## 3. 設定頁統一化
- [ ] 3.1 抽取 Wi-Fi/HomeKit 頁面共用的版面骨架（標題、狀態摘要卡、表單區）。
- [ ] 3.2 套用至 `/wifi`、`/homekit`，把相關操作（掃描/配對）與狀態放同一區塊並驗證表單照常送出。

## 4. 驗證與文件
- [ ] 4.1 更新 `README_TW.md`/`README.md` 或其他文件，描述新的版面與使用方式。
- [ ] 4.2 以 `python3 scripts/quick_check.py <device_ip> 8080` 驗證主要頁面回應，並記錄結果。
