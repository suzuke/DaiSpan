# DaiSpan 長期監控測試工具

## 工具說明

本目錄包含三個用於長期監控 ESP32-C3 DaiSpan 設備穩定性的工具：

### 1. 快速狀態檢查 (quick_check.py)

用於快速驗證設備是否正常運行。

```bash
# 自動掃描設備
python3 scripts/quick_check.py

# 指定設備IP
python3 scripts/quick_check.py 192.168.4.1
```

**功能：**
- 自動掃描常見IP地址尋找DaiSpan設備
- 測試核心頁面（/, /wifi, /homekit），並顯示回應時間與頁面大小
- 顯示回應時間和頁面大小
- 檢查頁面內容完整性

### 2. 長期穩定性測試 (long_term_test.py)

用於長時間監控設備穩定性，生成詳細報告。

```bash
# 單次測試
python3 scripts/long_term_test.py single 192.168.4.1

# 24小時測試，每5分鐘檢查一次
python3 scripts/long_term_test.py 192.168.4.1 24 5

# 2小時測試，每1分鐘檢查一次
python3 scripts/long_term_test.py 192.168.4.1 2 1
```

**功能：**
- 定期測試所有網頁功能
- 監控記憶體使用情況
- 記錄回應時間統計
- 生成詳細的JSON和文字報告
- 自動處理錯誤和異常

**輸出檔案：**
- `daispan_test_YYYYMMDD_HHMMSS.log` - 測試過程日誌
- `daispan_report_YYYYMMDD_HHMMSS.json` - 詳細測試報告

### 3. 系統資源監控 (resource_monitor.sh)

輕量級監控腳本，適合長期後台運行。

```bash
# 預設監控（每60秒檢查一次）
./scripts/resource_monitor.sh 192.168.4.1

# 自訂監控間隔（每30秒檢查一次）
./scripts/resource_monitor.sh 192.168.4.1 30
```

**功能：**
- 輕量級HTTP狀態檢查
- 記錄設備可用性統計
- 後台運行友好
- 支援Ctrl+C優雅退出

## 測試場景推薦

### 1. 初始驗證
```bash
# 確認設備正常啟動
python3 scripts/quick_check.py
```

### 2. 短期穩定性測試（開發階段）
```bash
# 2小時測試，每1分鐘檢查
python3 scripts/long_term_test.py 192.168.4.1 2 1
```

### 3. 長期穩定性測試（生產驗證）
```bash
# 24小時測試，每5分鐘檢查
python3 scripts/long_term_test.py 192.168.4.1 24 5
```

### 4. 持續監控（部署後）
```bash
# 後台持續監控
nohup ./scripts/resource_monitor.sh 192.168.4.1 60 > monitor.log 2>&1 &
```

## 預期問題和解決方案

### 1. 設備IP地址變更
- **問題**：設備重啟後IP地址可能變更
- **解決**：使用快速檢查的自動掃描功能，或檢查路由器DHCP分配

### 2. 記憶體不足導致回應緩慢
- **症狀**：回應時間超過5秒，部分頁面返回錯誤
- **監控**：長期測試會記錄記憶體使用情況

### 3. WiFi連接不穩定
- **症狀**：間歇性連接失敗
- **監控**：資源監控腳本會統計連接成功率

### 4. WebServer衝突
- **症狀**：HomeKit配對期間部分頁面無法訪問
- **說明**：這是正常行為，配對完成後會恢復

## 報告解讀

### 成功率指標
- **> 95%**：設備穩定運行
- **80-95%**：偶發性問題，需要關注
- **< 80%**：嚴重穩定性問題

### 回應時間指標
- **< 2秒**：正常
- **2-5秒**：可接受
- **> 5秒**：可能記憶體不足或系統負載過高

### 記憶體指標
- **可用記憶體 > 50KB**：正常
- **可用記憶體 20-50KB**：需要關注
- **可用記憶體 < 20KB**：記憶體緊張

## 依賴安裝

```bash
# 安裝Python依賴
pip3 install requests

# 安裝系統工具（macOS）
brew install bc curl
```

## 使用技巧

1. **後台運行**：使用 `nohup` 命令讓測試在後台運行
2. **定期檢查**：設置cron job進行定期檢查
3. **日誌輪替**：長期運行時注意日誌檔案大小
4. **網路穩定**：確保測試環境網路穩定

## 故障排除

### 設備無法連接
1. 檢查設備是否正常開機（藍色LED）
2. 確認設備在AP模式（查看WiFi列表中的"DaiSpan-Config"）
3. 檢查本機網路連接

### 測試腳本錯誤
1. 確認Python版本（建議3.7+）
2. 安裝必要的Python套件
3. 檢查設備IP地址是否正確

### 記憶體不足
1. 減少測試頻率
2. 檢查是否有記憶體洩漏
3. 考慮重啟設備