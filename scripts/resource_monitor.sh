#!/bin/bash
# DaiSpan 資源監控腳本
# 用於長期監控設備的系統資源使用情況

DEVICE_IP=${1:-"192.168.4.1"}
LOG_FILE="daispan_resource_$(date +%Y%m%d_%H%M%S).log"
INTERVAL=${2:-60}  # 預設每60秒檢查一次

echo "=== DaiSpan 資源監控開始 ===" | tee -a "$LOG_FILE"
echo "設備IP: $DEVICE_IP" | tee -a "$LOG_FILE"
echo "監控間隔: ${INTERVAL}秒" | tee -a "$LOG_FILE"
echo "日誌檔案: $LOG_FILE" | tee -a "$LOG_FILE"
echo "開始時間: $(date)" | tee -a "$LOG_FILE"
echo "========================================" | tee -a "$LOG_FILE"

# 計數器
SUCCESS_COUNT=0
FAIL_COUNT=0
TOTAL_COUNT=0

# 信號處理函數
cleanup() {
    echo "" | tee -a "$LOG_FILE"
    echo "=== 監控結束 ===" | tee -a "$LOG_FILE"
    echo "結束時間: $(date)" | tee -a "$LOG_FILE"
    echo "總檢查次數: $TOTAL_COUNT" | tee -a "$LOG_FILE"
    echo "成功次數: $SUCCESS_COUNT" | tee -a "$LOG_FILE"
    echo "失敗次數: $FAIL_COUNT" | tee -a "$LOG_FILE"
    if [ $TOTAL_COUNT -gt 0 ]; then
        SUCCESS_RATE=$(echo "scale=1; $SUCCESS_COUNT * 100 / $TOTAL_COUNT" | bc)
        echo "成功率: ${SUCCESS_RATE}%" | tee -a "$LOG_FILE"
    fi
    echo "日誌檔案: $LOG_FILE" | tee -a "$LOG_FILE"
    exit 0
}

# 捕獲中斷信號
trap cleanup SIGINT SIGTERM

# 檢查必要工具
if ! command -v curl &> /dev/null; then
    echo "錯誤: 需要安裝 curl" | tee -a "$LOG_FILE"
    exit 1
fi

# 主監控循環
while true; do
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    
    echo -n "[$TIMESTAMP] 檢查 #$TOTAL_COUNT: " | tee -a "$LOG_FILE"
    
    # 檢查主頁回應時間和狀態
    START_TIME=$(date +%s.%N)
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" --connect-timeout 10 --max-time 15 "http://$DEVICE_IP/")
    END_TIME=$(date +%s.%N)
    RESPONSE_TIME=$(echo "$END_TIME - $START_TIME" | bc)
    
    if [ "$HTTP_CODE" = "200" ]; then
        echo "✓ 正常 (HTTP $HTTP_CODE, ${RESPONSE_TIME}s)" | tee -a "$LOG_FILE"
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        
        # 嘗試獲取詳細狀態資訊
        STATUS_INFO=$(curl -s --connect-timeout 5 --max-time 10 "http://$DEVICE_IP/" | grep -E "(可用記憶體|Free Heap|WiFi|HomeKit)" | head -3 | tr '\n' ' ')
        if [ -n "$STATUS_INFO" ]; then
            echo "    狀態: $STATUS_INFO" | tee -a "$LOG_FILE"
        fi
        
    elif [ "$HTTP_CODE" = "000" ]; then
        echo "✗ 連接失敗 (無回應)" | tee -a "$LOG_FILE"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    else
        echo "✗ HTTP錯誤 ($HTTP_CODE, ${RESPONSE_TIME}s)" | tee -a "$LOG_FILE"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
    
    # 每10次檢查顯示統計
    if [ $((TOTAL_COUNT % 10)) -eq 0 ]; then
        if [ $TOTAL_COUNT -gt 0 ]; then
            SUCCESS_RATE=$(echo "scale=1; $SUCCESS_COUNT * 100 / $TOTAL_COUNT" | bc)
            echo "    [統計] 成功率: ${SUCCESS_RATE}% ($SUCCESS_COUNT/$TOTAL_COUNT)" | tee -a "$LOG_FILE"
        fi
    fi
    
    # 等待下次檢查
    sleep "$INTERVAL"
done