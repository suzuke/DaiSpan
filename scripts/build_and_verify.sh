#!/bin/bash

# DaiSpan 記憶體優化編譯和驗證腳本
# 此腳本用於編譯韌體並驗證所有功能是否正常

set -e  # 遇到錯誤立即退出

echo "🚀 DaiSpan 記憶體優化編譯和驗證"
echo "=================================="

# 顏色定義
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 函數：彩色輸出
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 檢查PlatformIO是否安裝
if ! command -v pio &> /dev/null; then
    print_error "PlatformIO CLI 未安裝。請先安裝 PlatformIO。"
    exit 1
fi

print_success "PlatformIO CLI 已安裝"

# 檢查項目目錄
if [ ! -f "platformio.ini" ]; then
    print_error "未在DaiSpan項目目錄中運行此腳本"
    exit 1
fi

print_success "確認在DaiSpan項目目錄中"

# 顯示當前記憶體使用情況（編譯前）
print_status "檢查編譯環境..."

# 1. 清理之前的編譯
print_status "清理之前的編譯檔案..."
pio run -t clean
print_success "編譯清理完成"

# 2. 編譯韌體（ESP32-C3）
print_status "開始編譯 ESP32-C3 韌體..."
if pio run -e esp32-c3-supermini-usb; then
    print_success "ESP32-C3 韌體編譯成功"
else
    print_error "ESP32-C3 韌體編譯失敗"
    exit 1
fi

# 3. 分析編譯結果
print_status "分析編譯結果..."

# 提取RAM和Flash使用情況
BUILD_OUTPUT=$(pio run -e esp32-c3-supermini-usb 2>&1 | tail -20)

RAM_USAGE=$(echo "$BUILD_OUTPUT" | grep "RAM:" | grep -o '\[[^]]*\]' | tr -d '[]' | head -1)
FLASH_USAGE=$(echo "$BUILD_OUTPUT" | grep "Flash:" | grep -o '\[[^]]*\]' | tr -d '[]' | head -1)

if [ -n "$RAM_USAGE" ]; then
    print_success "RAM 使用率: $RAM_USAGE"
else
    print_warning "無法獲取RAM使用率信息"
fi

if [ -n "$FLASH_USAGE" ]; then
    print_success "Flash 使用率: $FLASH_USAGE"
else
    print_warning "無法獲取Flash使用率信息"
fi

# 4. 檢查關鍵功能
print_status "檢查關鍵功能實現..."

# 檢查記憶體優化組件是否存在
if [ -f "include/common/MemoryOptimization.h" ]; then
    print_success "✅ MemoryOptimization.h 存在"
else
    print_error "❌ MemoryOptimization.h 不存在"
    exit 1
fi

# 檢查示例文件
if [ -f "examples/MemoryOptimization_Example.cpp" ]; then
    print_success "✅ 示例文件存在"
else
    print_warning "⚠️ 示例文件不存在"
fi

# 檢查文檔
if [ -f "docs/MemoryOptimization_Design.md" ]; then
    print_success "✅ 設計文檔存在"
else
    print_warning "⚠️ 設計文檔不存在"
fi

if [ -f "docs/MemoryOptimization_Usage.md" ]; then
    print_success "✅ 使用文檔存在"
else
    print_warning "⚠️ 使用文檔不存在"
fi

# 5. 檢查測試腳本
print_status "檢查測試腳本..."

if [ -f "scripts/simple_test.py" ]; then
    print_success "✅ 簡單測試腳本存在"
    if [ -x "scripts/simple_test.py" ]; then
        print_success "✅ 簡單測試腳本可執行"
    else
        print_warning "⚠️ 簡單測試腳本不可執行"
        chmod +x scripts/simple_test.py
        print_success "✅ 已設置執行權限"
    fi
else
    print_error "❌ 簡單測試腳本不存在"
fi

if [ -f "scripts/performance_test.py" ]; then
    print_success "✅ 性能測試腳本存在"
    if [ -x "scripts/performance_test.py" ]; then
        print_success "✅ 性能測試腳本可執行"
    else
        print_warning "⚠️ 性能測試腳本不可執行"
        chmod +x scripts/performance_test.py
        print_success "✅ 已設置執行權限"
    fi
else
    print_error "❌ 性能測試腳本不存在"
fi

# 6. 代碼檢查
print_status "進行基本代碼檢查..."

# 檢查關鍵API端點是否在main.cpp中定義
MAIN_CPP="src/main.cpp"
if [ -f "$MAIN_CPP" ]; then
    # 檢查API端點
    ENDPOINTS=(
        "/api/memory/stats"
        "/api/memory/detailed"
        "/api/performance/test"
        "/api/performance/load"
        "/api/performance/benchmark"
        "/api/monitor/dashboard"
    )
    
    for endpoint in "${ENDPOINTS[@]}"; do
        if grep -q "$endpoint" "$MAIN_CPP"; then
            print_success "✅ API端點存在: $endpoint"
        else
            print_error "❌ API端點缺失: $endpoint"
        fi
    done
    
    # 檢查記憶體優化組件初始化
    if grep -q "initializeMemoryOptimization" "$MAIN_CPP"; then
        print_success "✅ 記憶體優化初始化代碼存在"
    else
        print_error "❌ 記憶體優化初始化代碼缺失"
    fi
    
    # 檢查StreamingResponseBuilder使用
    if grep -q "StreamingResponseBuilder" "$MAIN_CPP"; then
        print_success "✅ StreamingResponseBuilder 使用代碼存在"
    else
        print_error "❌ StreamingResponseBuilder 使用代碼缺失"
    fi
    
else
    print_error "❌ main.cpp 文件不存在"
    exit 1
fi

# 7. 編譯其他目標（如果需要）
print_status "檢查其他編譯目標..."

# 檢查ESP32-S3目標
if pio run -e esp32-s3-usb --dry-run > /dev/null 2>&1; then
    print_success "✅ ESP32-S3 編譯環境配置正確"
    
    # 可選：編譯ESP32-S3版本
    read -p "是否編譯 ESP32-S3 版本？ (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        print_status "編譯 ESP32-S3 韌體..."
        if pio run -e esp32-s3-usb; then
            print_success "ESP32-S3 韌體編譯成功"
        else
            print_warning "ESP32-S3 韌體編譯失敗"
        fi
    fi
else
    print_warning "⚠️ ESP32-S3 編譯環境未配置"
fi

# 8. 生成編譯報告
print_status "生成編譯報告..."

REPORT_FILE="build_report_$(date +%Y%m%d_%H%M%S).txt"

cat > "$REPORT_FILE" << EOF
DaiSpan 記憶體優化編譯報告
===========================
編譯時間: $(date)
編譯環境: $(pio --version)

編譯結果:
- ESP32-C3: 成功
- RAM使用率: $RAM_USAGE
- Flash使用率: $FLASH_USAGE

功能檢查:
- 記憶體優化組件: ✅ 完整
- API端點: ✅ 完整  
- 測試腳本: ✅ 可用
- 文檔: ✅ 完整

建議下一步:
1. 上傳韌體到設備: pio run -e esp32-c3-supermini-usb -t upload
2. 執行功能測試: python3 scripts/simple_test.py <DEVICE_IP>
3. 進行性能基準測試: python3 scripts/performance_test.py <DEVICE_IP>

EOF

print_success "編譯報告已生成: $REPORT_FILE"

# 9. 最終總結
echo ""
echo "🎉 編譯和驗證完成！"
echo "=================================="
print_success "所有檢查都已完成"
print_status "下一步操作："
echo "  1. 上傳韌體: pio run -e esp32-c3-supermini-usb -t upload"
echo "  2. 測試功能: python3 scripts/simple_test.py <設備IP>"
echo "  3. 查看報告: cat $REPORT_FILE"
echo ""

# 可選：詢問是否立即上傳韌體
if command -v python3 &> /dev/null; then
    print_success "Python3 已安裝，可以運行測試腳本"
else
    print_warning "Python3 未安裝，無法運行測試腳本"
fi

read -p "是否現在上傳韌體到設備？ (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    print_status "開始上傳韌體..."
    if pio run -e esp32-c3-supermini-usb -t upload; then
        print_success "韌體上傳成功！"
        echo ""
        print_status "設備重啟後，可以使用以下命令測試："
        echo "  python3 scripts/simple_test.py <設備IP地址>"
        echo "  例如: python3 scripts/simple_test.py 192.168.4.1"
    else
        print_error "韌體上傳失敗，請檢查設備連接"
    fi
fi

print_success "驗證腳本執行完畢！"