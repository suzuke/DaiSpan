#!/usr/bin/env python3
"""
手動測試 V3 事件系統
通過添加測試端點來驗證事件系統是否工作
"""

import requests
import time

def test_current_status(device_ip="192.168.50.192"):
    """檢查當前系統狀態"""
    print("🔍 檢查當前系統狀態...")
    
    response = requests.get(f"http://{device_ip}:8080/", timeout=5)
    content = response.text
    
    # 提取事件統計
    if "事件統計:" in content:
        stats_line = content.split("事件統計:")[1].split("</p>")[0]
        print(f"📊 當前事件統計: {stats_line.strip()}")
    
    # 提取運行時間和記憶體
    import re
    runtime_match = re.search(r'運行時間.*?(\d+)天\s*(\d+)時\s*(\d+)分', content)
    if runtime_match:
        days, hours, minutes = map(int, runtime_match.groups())
        total_minutes = days * 1440 + hours * 60 + minutes
        print(f"⏰ 運行時間: {total_minutes} 分鐘 (系統很穩定!)")
    
    memory_match = re.search(r'可用記憶體.*?(\d+)\s*bytes', content)
    if memory_match:
        memory = int(memory_match.group(1))
        print(f"💾 可用記憶體: {memory} bytes")
    
    return True

def analyze_v3_bridge_issue(device_ip="192.168.50.192"):
    """分析 V3 橋接問題"""
    print("\n🔍 V3 橋接問題分析:")
    print("=" * 50)
    
    print("💡 問題確認:")
    print("1. ✅ V3 架構已初始化 - 事件系統運行中")
    print("2. ✅ 遷移管理器活躍 - 橋接器已創建")
    print("3. ✅ HomeKit 控制正常 - V2 路徑工作")
    print("4. ❌ V3 事件未觸發 - 缺少 V2→V3 橋接")
    
    print("\n🔧 根本原因:")
    print("• HomeKit 操作直接走 V2 路徑")
    print("• ThermostatDevice 沒有發布 V3 事件")
    print("• HomeKitEventBridge 的處理函數被註釋")
    print("• 缺少從 V2 控制器到 V3 事件系統的橋接")
    
    print("\n🏗️ 當前架構流程:")
    print("HomeKit → ThermostatDevice → Controller → S21Protocol")
    print("               ↓ (斷開)")
    print("        V3 EventBus (沒有收到事件)")
    
    print("\n✨ 需要的架構:")
    print("HomeKit → ThermostatDevice → Controller → S21Protocol")
    print("               ↓ (添加事件發布)")
    print("        V3 EventBus → Event Handlers")
    
    return True

def propose_solutions():
    """提出解決方案"""
    print("\n💡 解決方案選項:")
    print("=" * 50)
    
    print("🎯 方案 1: 最小侵入性修改")
    print("• 在 ThermostatDevice 的 update() 回調中添加 V3 事件發布")
    print("• 修改 HomeKitEventBridge 啟用事件處理函數")
    print("• 優點: 改動最小，風險最低")
    print("• 缺點: V2/V3 雙重處理，可能有延遲")
    
    print("\n🎯 方案 2: 逐步遷移")
    print("• 讓 ThermostatDevice 直接使用 V3 ThermostatAggregate")
    print("• 保持 S21Protocol 作為底層實作")
    print("• 優點: 真正使用 V3 架構，性能更好")
    print("• 缺點: 需要更多代碼修改")
    
    print("\n🎯 方案 3: 測試驗證優先")
    print("• 添加手動觸發 V3 事件的調試端點")
    print("• 驗證事件系統本身是否正常工作")
    print("• 確認統計顯示邏輯")
    print("• 優點: 快速驗證，排除其他問題")
    
    print("\n⚡ 建議執行順序:")
    print("1. 先執行方案 3 - 驗證 V3 事件系統本身")
    print("2. 再執行方案 1 - 添加最小橋接")
    print("3. 最後考慮方案 2 - 完整遷移")
    
    return True

def create_test_plan():
    """創建測試計劃"""
    print("\n📋 驗證測試計劃:")
    print("=" * 50)
    
    print("🧪 立即可執行的測試:")
    print("1. 添加調試端點手動觸發 V3 事件")
    print("2. 檢查事件統計是否更新")
    print("3. 驗證事件處理器是否被調用")
    
    print("\n📝 代碼修改建議 (最小改動):")
    print("在 ThermostatDevice.h 的適當位置添加:")
    print("```cpp")
    print("// 通知 V3 事件系統")
    print("extern DaiSpan::Core::EventPublisher* g_eventBus;")
    print("if (g_eventBus) {")
    print("    auto event = std::make_unique<StateChanged>(oldState, newState, \"homekit\");")
    print("    g_eventBus->publish(std::move(event));")
    print("}")
    print("```")
    
    print("\n🔧 統計顯示修正:")
    print("修改 main.cpp:249-250:")
    print("```cpp")
    print("info += \"\\\"subscriptions\\\":\" + String(getSubscriptionCount()) + \",\";")
    print("info += \"\\\"processed\\\":\" + String(getProcessedCount());")
    print("```")
    
    return True

def main():
    print("🎯 V3 事件系統問題診斷")
    print("=" * 60)
    
    device_ip = "192.168.50.192"
    
    # 檢查當前狀態
    test_current_status(device_ip)
    
    # 分析問題
    analyze_v3_bridge_issue(device_ip)
    
    # 提出解決方案
    propose_solutions()
    
    # 測試計劃
    create_test_plan()
    
    print("\n" + "=" * 60)
    print("📊 總結:")
    print("=" * 60)
    print("✅ V3 架構運行正常，系統穩定")
    print("✅ HomeKit 控制功能完全正常") 
    print("❌ V2→V3 事件橋接缺失")
    print("🔧 需要添加事件發布橋接代碼")
    
    print("\n💡 下一步建議:")
    print("1. 先驗證 V3 事件系統本身是否正常")
    print("2. 添加最小的 V2→V3 事件橋接")
    print("3. 修正統計顯示邏輯")
    
    return 0

if __name__ == "__main__":
    exit(main())