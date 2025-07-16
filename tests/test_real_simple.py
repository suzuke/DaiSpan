#!/usr/bin/env python3
"""
簡化的真實空調測試腳本
專門測試 V3 架構在真實環境下的核心功能
"""

import requests
import time
import re
from typing import Dict, Any

def test_real_ac_status(device_ip: str = "192.168.50.192") -> Dict[str, Any]:
    """測試真實空調狀態"""
    print("🔍 檢查真實空調系統狀態...")
    
    try:
        # 1. 主頁狀態檢查
        response = requests.get(f"http://{device_ip}:8080/", timeout=10)
        main_page = response.text
        
        results = {
            "connection_ok": response.status_code == 200,
            "v3_active": "V3 事件驅動" in main_page,
            "migration_active": "✅ 活躍" in main_page,
            "real_mode": "切換模擬模式" in main_page,
            "runtime_minutes": 0,
            "free_memory": 0
        }
        
        # 解析運行時間
        runtime_match = re.search(r'(\d+)天\s*(\d+)時\s*(\d+)分', main_page)
        if runtime_match:
            days, hours, minutes = map(int, runtime_match.groups())
            results["runtime_minutes"] = days * 1440 + hours * 60 + minutes
        
        # 解析記憶體
        memory_match = re.search(r'(\d+)\s*bytes', main_page)
        if memory_match:
            results["free_memory"] = int(memory_match.group(1))
        
        # 2. 模式確認檢查
        mode_response = requests.get(f"http://{device_ip}:8080/simulation-toggle", timeout=5)
        mode_page = mode_response.text
        results["mode_confirmed"] = "🏭 真實模式" in mode_page
        
        return results
        
    except Exception as e:
        return {"error": str(e), "connection_ok": False}

def test_homekit_readiness(device_ip: str = "192.168.50.192") -> Dict[str, Any]:
    """測試 HomeKit 控制準備度"""
    print("🏠 檢查 HomeKit 控制準備度...")
    
    try:
        # HomeKit 設定頁面
        homekit_response = requests.get(f"http://{device_ip}:8080/homekit", timeout=5)
        homekit_ok = homekit_response.status_code == 200 and "HomeKit" in homekit_response.text
        
        # 系統響應測試
        start_time = time.time()
        status_response = requests.get(f"http://{device_ip}:8080/", timeout=5)
        response_time = time.time() - start_time
        
        return {
            "homekit_page_accessible": homekit_ok,
            "system_responsive": response_time < 2.0,
            "response_time": response_time,
            "ready_for_control": homekit_ok and response_time < 2.0
        }
        
    except Exception as e:
        return {"error": str(e)}

def monitor_system_stability(device_ip: str = "192.168.50.192", readings: int = 5) -> Dict[str, Any]:
    """監控系統穩定性"""
    print(f"📊 監控系統穩定性 ({readings} 次讀取)...")
    
    memory_readings = []
    response_times = []
    
    for i in range(readings):
        try:
            start_time = time.time()
            response = requests.get(f"http://{device_ip}:8080/", timeout=5)
            response_time = time.time() - start_time
            
            response_times.append(response_time)
            
            # 解析記憶體
            memory_match = re.search(r'(\d+)\s*bytes', response.text)
            if memory_match:
                memory = int(memory_match.group(1))
                memory_readings.append(memory)
                print(f"   讀取 {i+1}: 記憶體 {memory} bytes, 響應時間 {response_time:.2f}s")
            else:
                memory_readings.append(0)
                print(f"   讀取 {i+1}: 記憶體解析失敗, 響應時間 {response_time:.2f}s")
                
        except Exception as e:
            print(f"   讀取 {i+1}: 失敗 - {e}")
            memory_readings.append(0)
            response_times.append(10.0)
            
        if i < readings - 1:
            time.sleep(3)
    
    # 分析結果
    valid_memory = [m for m in memory_readings if m > 0]
    valid_response = [r for r in response_times if r < 5.0]
    
    if not valid_memory:
        return {"error": "無法獲取有效的記憶體資料"}
    
    return {
        "memory_readings": memory_readings,
        "response_times": response_times,
        "memory_min": min(valid_memory),
        "memory_max": max(valid_memory),
        "memory_avg": sum(valid_memory) / len(valid_memory),
        "memory_variance": max(valid_memory) - min(valid_memory),
        "response_avg": sum(valid_response) / len(valid_response) if valid_response else 10.0,
        "stable": (max(valid_memory) - min(valid_memory)) < 15000,  # 允許 15KB 變化
        "responsive": (sum(valid_response) / len(valid_response) if valid_response else 10.0) < 1.0
    }

def main():
    print("🎯 V3 真實空調環境簡化測試")
    print("=" * 50)
    
    device_ip = "192.168.50.192"
    
    # 1. 系統狀態檢查
    print("\n📊 步驟 1: 系統狀態檢查")
    status = test_real_ac_status(device_ip)
    
    if not status.get("connection_ok"):
        print("❌ 設備連接失敗")
        return 1
    
    print(f"✅ 設備連接正常")
    print(f"V3 架構: {'✅' if status.get('v3_active') else '❌'}")
    print(f"遷移管理器: {'✅' if status.get('migration_active') else '❌'}")
    print(f"真實模式: {'✅' if status.get('mode_confirmed') else '❌'}")
    print(f"運行時間: {status.get('runtime_minutes', 0)} 分鐘")
    print(f"可用記憶體: {status.get('free_memory', 0)} bytes")
    
    # 2. HomeKit 準備度檢查
    print("\n📊 步驟 2: HomeKit 控制準備度")
    homekit = test_homekit_readiness(device_ip)
    
    if "error" not in homekit:
        print(f"HomeKit 頁面: {'✅' if homekit.get('homekit_page_accessible') else '❌'}")
        print(f"系統響應: {'✅' if homekit.get('system_responsive') else '❌'} ({homekit.get('response_time', 0):.2f}s)")
        print(f"控制準備度: {'✅' if homekit.get('ready_for_control') else '❌'}")
    else:
        print(f"❌ HomeKit 測試失敗: {homekit['error']}")
    
    # 3. 穩定性監控
    print("\n📊 步驟 3: 穩定性監控")
    stability = monitor_system_stability(device_ip, 5)
    
    if "error" not in stability:
        print(f"記憶體穩定性: {'✅' if stability.get('stable') else '❌'}")
        print(f"系統響應性: {'✅' if stability.get('responsive') else '❌'}")
        print(f"平均記憶體: {stability.get('memory_avg', 0):.0f} bytes")
        print(f"記憶體變化: {stability.get('memory_variance', 0)} bytes")
        print(f"平均響應時間: {stability.get('response_avg', 0):.2f}s")
    else:
        print(f"❌ 穩定性測試失敗: {stability['error']}")
    
    # 4. 總結
    print("\n" + "=" * 50)
    print("📊 測試總結")
    print("=" * 50)
    
    score = 0
    max_score = 8
    
    # 系統狀態評分 (4分)
    if status.get("v3_active"): score += 1
    if status.get("migration_active"): score += 1
    if status.get("mode_confirmed"): score += 1
    if status.get("free_memory", 0) > 100000: score += 1
    
    # HomeKit 準備度評分 (2分)
    if homekit.get("homekit_page_accessible"): score += 1
    if homekit.get("ready_for_control"): score += 1
    
    # 穩定性評分 (2分)
    if stability.get("stable"): score += 1
    if stability.get("responsive"): score += 1
    
    success_rate = (score / max_score) * 100
    print(f"總體評分: {score}/{max_score} ({success_rate:.1f}%)")
    
    if success_rate >= 90:
        print("🎉 V3 真實空調系統運行優秀！")
        print("💡 系統已準備好接受 HomeKit 控制命令")
        result = 0
    elif success_rate >= 75:
        print("✅ V3 真實空調系統運行良好！")
        print("💡 可以進行 HomeKit 控制測試")
        result = 0
    elif success_rate >= 50:
        print("⚠️ V3 真實空調系統基本正常")
        print("💡 建議先解決穩定性問題再進行控制測試")
        result = 0
    else:
        print("❌ V3 真實空調系統存在問題")
        print("💡 需要檢查系統配置和連接狀態")
        result = 1
    
    return result

if __name__ == "__main__":
    exit(main())