#!/usr/bin/env python3
"""
觸發 V3 事件測試腳本
通過操作真實空調來觸發 V3 架構中的事件
"""

import requests
import time

def trigger_v3_events(device_ip="192.168.50.192"):
    """
    觸發 V3 事件的幾種方法：
    1. 通過 HomeKit 操作（如果在真實模式下）
    2. 檢查是否有可用的調試端點
    3. 檢查事件統計變化
    """
    
    print("🔍 檢查當前 V3 事件統計...")
    
    # 獲取初始統計
    response = requests.get(f"http://{device_ip}:8080/", timeout=5)
    content = response.text
    
    print("📊 當前頁面事件統計:")
    if "事件統計:" in content:
        stats_line = content.split("事件統計:")[1].split("</p>")[0]
        print(f"   {stats_line}")
    
    print("\n🔍 分析問題原因...")
    
    # 檢查 V3 架構是否真的啟用
    if "V3 事件驅動" in content:
        print("✅ V3 架構已啟用")
    else:
        print("❌ V3 架構未啟用")
        
    if "✅ 活躍" in content:
        print("✅ 遷移管理器活躍")
    else:
        print("❌ 遷移管理器不活躍")
    
    print("\n💡 可能的原因分析:")
    print("1. 事件統計顯示邏輯問題:")
    print("   - main.cpp:249 行硬編碼 'subscriptions':0")
    print("   - 缺少 getSubscriptionCount() 方法")
    print("   - 缺少已處理事件計數器")
    
    print("\n2. 事件觸發條件:")
    print("   - V3 事件主要由 HomeKit 操作觸發")
    print("   - 真實模式下需要實際控制空調")
    print("   - 溫度變化需要達到閾值才觸發事件")
    
    print("\n3. 檢查事件訂閱情況:")
    print("   - 已確認有 StateChanged 和 Error 事件訂閱")
    print("   - processV3Events() 在主循環中被調用")
    print("   - 遷移管理器正在運行")
    
    print("\n🧪 建議的解決方案:")
    print("1. 修正統計顯示邏輯")
    print("2. 添加事件計數器")
    print("3. 通過 HomeKit 手動觸發事件")
    print("4. 檢查事件閾值設定")
    
    return True

def check_event_system_internals(device_ip="192.168.50.192"):
    """檢查事件系統內部狀況"""
    
    print("\n🔧 檢查事件系統內部狀況...")
    
    # 檢查是否有調試端點
    debug_endpoints = [
        "/debug",
        "/api/v3/events",
        "/api/v3/stats", 
        "/api/status"
    ]
    
    for endpoint in debug_endpoints:
        try:
            response = requests.get(f"http://{device_ip}:8080{endpoint}", timeout=3)
            if response.status_code == 200:
                print(f"✅ {endpoint} 可訪問 (HTTP {response.status_code})")
                if "event" in response.text.lower() or "stats" in response.text.lower():
                    print(f"   包含事件/統計相關資訊")
            elif response.status_code == 404:
                print(f"❌ {endpoint} 不存在 (HTTP 404)")
            else:
                print(f"⚠️ {endpoint} 異常 (HTTP {response.status_code})")
        except Exception as e:
            print(f"❌ {endpoint} 連接失敗: {e}")
    
    return True

def main():
    print("🎯 V3 事件系統分析工具")
    print("=" * 50)
    
    device_ip = "192.168.50.192"
    
    # 基本檢查
    try:
        response = requests.get(f"http://{device_ip}:8080/", timeout=5)
        if response.status_code != 200:
            print("❌ 設備不可達")
            return 1
    except Exception as e:
        print(f"❌ 設備連接失敗: {e}")
        return 1
    
    # 觸發和分析事件
    trigger_v3_events(device_ip)
    
    # 檢查系統內部
    check_event_system_internals(device_ip)
    
    print("\n" + "=" * 50)
    print("📋 結論:")
    print("=" * 50)
    
    print("問題原因：")
    print("1. 統計顯示邏輯不完整 - 硬編碼為 0")
    print("2. 缺少 getSubscriptionCount() 方法")
    print("3. 缺少已處理事件計數器")
    print("4. 真實事件需要 HomeKit 操作觸發")
    
    print("\n解決方案：")
    print("1. 透過 HomeKit 應用程式控制空調")
    print("2. 觀察事件佇列大小變化")
    print("3. 檢查調試日志輸出")
    print("4. 考慮添加調試端點顯示真實統計")
    
    print("\n💡 立即測試：")
    print("請使用 HomeKit 應用程式：")
    print("- 調整目標溫度")
    print("- 切換運行模式")
    print("- 開關空調電源")
    print("然後重新檢查事件統計")
    
    return 0

if __name__ == "__main__":
    exit(main())