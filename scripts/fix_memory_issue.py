#!/usr/bin/env python3
"""
DaiSpan Memory Issue Fix Script
修復記憶體不足導致 WebServer 無法啟動的問題
"""

import subprocess
import sys
import os

def main():
    print("🔧 DaiSpan WebServer 記憶體修復工具")
    print("=" * 50)
    
    # 檢查是否在正確的目錄
    if not os.path.exists("platformio.ini"):
        print("❌ 請在 DaiSpan 項目根目錄運行此腳本")
        return
    
    print("📝 已應用的修復：")
    print("1. ✅ 降低記憶體門檻：70KB → 60KB")
    print("2. ✅ 添加記憶體釋放邏輯")
    print("3. ✅ 改善記憶體監控")
    
    print("\n🔨 正在重新編譯固件...")
    try:
        result = subprocess.run(['pio', 'run', '-e', 'esp32-c3-supermini-ota'], 
                              capture_output=True, text=True)
        if result.returncode == 0:
            print("✅ 編譯成功！")
            print("\n📤 上傳新固件:")
            print("   pio run -e esp32-c3-supermini-ota -t upload")
        else:
            print("❌ 編譯失敗:")
            print(result.stderr)
    except Exception as e:
        print(f"❌ 執行失敗: {e}")
    
    print("\n🎯 修復後的行為:")
    print("- 當記憶體 ≥ 60KB 時，WebServer 將正常啟動")
    print("- 當記憶體 < 60KB 時，會嘗試釋放資源")
    print("- 更詳細的記憶體使用日誌")
    
    print("\n📋 驗證步驟:")
    print("1. 上傳新固件到設備")
    print("2. 重啟設備")
    print("3. 監控串口輸出")
    print("4. 查看 'WebServer監控功能已啟動' 信息")
    print("5. 嘗試訪問 http://192.168.50.192:8080/")
    
    print("\n💡 如果問題仍存在:")
    print("- 考慮關閉不必要的功能")
    print("- 檢查是否有記憶體洩漏")
    print("- 考慮優化事件系統配置")

if __name__ == "__main__":
    main()