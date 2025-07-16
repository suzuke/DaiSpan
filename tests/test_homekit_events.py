#!/usr/bin/env python3
"""
測試 HomeKit 操作是否觸發 V3 事件
"""

import requests
import time
import re

def get_event_stats(device_ip="192.168.50.192"):
    """獲取當前事件統計"""
    try:
        response = requests.get(f"http://{device_ip}:8080/", timeout=5)
        content = response.text
        
        # 提取事件統計
        stats_match = re.search(r'事件統計.*?佇列:(\d+)\s*訂閱:(\d+)\s*已處理:(\d+)', content, re.DOTALL)
        if stats_match:
            queue_size = int(stats_match.group(1))
            subscriptions = int(stats_match.group(2))
            processed = int(stats_match.group(3))
            return queue_size, subscriptions, processed
        
        print(f"⚠️ 未找到事件統計匹配")
        return None, None, None
    except Exception as e:
        print(f"❌ 獲取統計失敗: {e}")
        return None, None, None

def test_simulation_mode_toggle(device_ip="192.168.50.192"):
    """測試模擬模式切換是否觸發事件"""
    print("🧪 測試模擬模式切換...")
    
    # 獲取初始統計
    initial_queue, initial_subs, initial_processed = get_event_stats(device_ip)
    print(f"初始統計: 佇列:{initial_queue} 訂閱:{initial_subs} 已處理:{initial_processed}")
    
    # 觸發模擬模式切換
    try:
        response = requests.get(f"http://{device_ip}:8080/simulation-toggle", timeout=5)
        print(f"模擬模式切換請求: HTTP {response.status_code}")
        
        # 等待事件處理
        time.sleep(2)
        
        # 檢查統計變化
        final_queue, final_subs, final_processed = get_event_stats(device_ip)
        print(f"最終統計: 佇列:{final_queue} 訂閱:{final_subs} 已處理:{final_processed}")
        
        if initial_processed is not None and final_processed is not None:
            if final_processed > initial_processed:
                print(f"✅ 事件被觸發! 已處理事件從 {initial_processed} 增加到 {final_processed}")
                return True
            else:
                print(f"❌ 事件未被觸發，已處理事件仍為 {final_processed}")
                return False
        else:
            print("⚠️ 無法解析統計數據")
            return False
            
    except Exception as e:
        print(f"❌ 模擬模式切換失敗: {e}")
        return False

def main():
    print("🎯 HomeKit 事件觸發測試")
    print("=" * 50)
    
    device_ip = "192.168.50.192"
    
    # 測試1: 檢查初始狀態
    print("\n1. 檢查初始 V3 事件統計...")
    initial_queue, initial_subs, initial_processed = get_event_stats(device_ip)
    
    if initial_subs is not None:
        print(f"📊 當前事件統計:")
        print(f"   佇列大小: {initial_queue}")
        print(f"   訂閱數量: {initial_subs}")
        print(f"   已處理事件: {initial_processed}")
    else:
        print("❌ 無法獲取事件統計")
        return 1
    
    # 測試2: 模擬模式切換
    print("\n2. 測試模擬模式切換...")
    if test_simulation_mode_toggle(device_ip):
        print("✅ HomeKit 操作成功觸發 V3 事件!")
    else:
        print("❌ HomeKit 操作未觸發 V3 事件")
    
    # 測試3: 檢查最終狀態
    print("\n3. 檢查最終事件統計...")
    final_queue, final_subs, final_processed = get_event_stats(device_ip)
    
    if final_subs is not None:
        print(f"📊 最終事件統計:")
        print(f"   佇列大小: {final_queue}")
        print(f"   訂閱數量: {final_subs}")
        print(f"   已處理事件: {final_processed}")
    
    print("\n" + "=" * 50)
    print("📋 測試結論:")
    print("=" * 50)
    
    if initial_processed is not None and final_processed is not None:
        if final_processed > initial_processed:
            print("✅ V3 事件橋接工作正常!")
            print(f"✅ 成功觸發了 {final_processed - initial_processed} 個事件")
            print("✅ HomeKit 操作現在會觸發 V3 事件系統")
        else:
            print("❌ V3 事件橋接需要進一步調試")
            print("❌ HomeKit 操作未觸發 V3 事件")
    
    print("\n💡 建議:")
    print("1. 通過 HomeKit 應用程式測試溫度和模式變更")
    print("2. 觀察事件統計的變化")
    print("3. 檢查調試日誌是否有 V3 事件發布訊息")
    
    return 0

if __name__ == "__main__":
    exit(main())