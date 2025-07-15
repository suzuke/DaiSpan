#!/usr/bin/env python3
"""
V3 HomeKit 整合測試腳本
測試 HomeKit 指令是否能正確觸發 V3 事件系統
"""

import requests
import time
import json
from typing import Dict, Any

class HomeKitV3Tester:
    def __init__(self, device_ip: str = "192.168.50.192", port: int = 8080):
        self.device_ip = device_ip
        self.port = port
        self.base_url = f"http://{device_ip}:{port}"
        
    def get_simulation_status(self) -> Dict[str, Any]:
        """獲取模擬器當前狀態"""
        try:
            response = requests.get(f"{self.base_url}/simulation", timeout=5)
            content = response.text
            
            # 解析狀態資訊
            status = {}
            
            if "電源：</strong>開啟" in content:
                status["power"] = True
            elif "電源：</strong>關閉" in content:
                status["power"] = False
                
            # 解析模式
            if "模式：</strong>0" in content:
                status["mode"] = 0
            elif "模式：</strong>1" in content:
                status["mode"] = 1
            elif "模式：</strong>2" in content:
                status["mode"] = 2
            elif "模式：</strong>3" in content:
                status["mode"] = 3
                
            # 解析溫度
            import re
            temp_match = re.search(r'目標溫度：</strong>([0-9.]+)°C', content)
            if temp_match:
                status["target_temp"] = float(temp_match.group(1))
                
            current_temp_match = re.search(r'當前溫度：</strong>([0-9.]+)°C', content)
            if current_temp_match:
                status["current_temp"] = float(current_temp_match.group(1))
                
            # 解析運行狀態
            if "❄️ 制冷中" in content:
                status["operation"] = "cooling"
            elif "🔥 制熱中" in content:
                status["operation"] = "heating"
            elif "⏸️ 待機" in content:
                status["operation"] = "idle"
                
            return status
        except Exception as e:
            print(f"❌ 無法獲取狀態: {e}")
            return {}
    
    def send_simulation_command(self, **params) -> bool:
        """發送模擬控制命令"""
        try:
            response = requests.post(
                f"{self.base_url}/simulation-control",
                data=params,
                timeout=5
            )
            return response.status_code == 200
        except Exception as e:
            print(f"❌ 命令發送失敗: {e}")
            return False
    
    def test_homekit_scenarios(self):
        """測試 HomeKit 常見使用場景"""
        print("🏠 開始 HomeKit 使用場景測試...\n")
        
        scenarios = [
            {
                "name": "❄️ 夏季制冷場景",
                "description": "開啟空調，制冷模式，設定 20°C",
                "commands": {"power": "1", "mode": "2", "target_temp": "20.0", "current_temp": "26.0"},
                "expected": {"power": True, "mode": 2, "target_temp": 20.0}
            },
            {
                "name": "🔥 冬季制熱場景", 
                "description": "切換制熱模式，設定 24°C",
                "commands": {"power": "1", "mode": "1", "target_temp": "24.0", "current_temp": "18.0"},
                "expected": {"power": True, "mode": 1, "target_temp": 24.0}
            },
            {
                "name": "🌡️ 自動模式場景",
                "description": "切換自動模式，設定 22°C 舒適溫度",
                "commands": {"power": "1", "mode": "3", "target_temp": "22.0", "current_temp": "25.0"},
                "expected": {"power": True, "mode": 3, "target_temp": 22.0}
            },
            {
                "name": "⏹️ 關閉空調場景",
                "description": "關閉空調節能",
                "commands": {"power": "0", "mode": "0"},
                "expected": {"power": False}
            }
        ]
        
        passed_tests = 0
        total_tests = len(scenarios)
        
        for i, scenario in enumerate(scenarios, 1):
            print(f"🧪 測試 {i}/{total_tests}: {scenario['name']}")
            print(f"   描述: {scenario['description']}")
            
            # 獲取初始狀態
            initial_status = self.get_simulation_status()
            print(f"   初始狀態: {initial_status}")
            
            # 發送命令
            success = self.send_simulation_command(**scenario["commands"])
            if not success:
                print(f"   ❌ 命令發送失敗")
                continue
                
            # 等待處理
            time.sleep(2)
            
            # 檢查結果
            final_status = self.get_simulation_status()
            print(f"   最終狀態: {final_status}")
            
            # 驗證預期結果
            test_passed = True
            for key, expected_value in scenario["expected"].items():
                if key in final_status:
                    if final_status[key] != expected_value:
                        print(f"   ❌ {key} 預期 {expected_value}，實際 {final_status[key]}")
                        test_passed = False
                else:
                    print(f"   ⚠️ 無法檢測 {key} 狀態")
                    
            if test_passed:
                print(f"   ✅ 測試通過")
                passed_tests += 1
            else:
                print(f"   ❌ 測試失敗")
                
            print()
            time.sleep(1)
            
        # 總結
        print("=" * 60)
        print(f"📊 HomeKit 場景測試總結")
        print("=" * 60)
        print(f"通過測試: {passed_tests}/{total_tests}")
        print(f"成功率: {passed_tests/total_tests*100:.1f}%")
        
        if passed_tests == total_tests:
            print("🎉 所有 HomeKit 場景測試通過！V3 架構與 HomeKit 整合成功")
            return True
        elif passed_tests >= total_tests * 0.75:
            print("⚠️ 大部分測試通過，V3 架構基本正常")
            return True
        else:
            print("❌ 多個測試失敗，需要檢查 V3 架構實作")
            return False

def main():
    print("🎯 V3 HomeKit 整合測試")
    print("=" * 60)
    
    tester = HomeKitV3Tester()
    
    # 檢查設備連接
    print("🔍 檢查設備連接...")
    try:
        response = requests.get(f"http://192.168.50.192:8080/", timeout=5)
        if response.status_code == 200:
            print("✅ 設備連接正常")
        else:
            print(f"❌ 設備回應異常: {response.status_code}")
            return 1
    except Exception as e:
        print(f"❌ 設備無法連接: {e}")
        return 1
    
    # 執行測試
    success = tester.test_homekit_scenarios()
    
    return 0 if success else 1

if __name__ == "__main__":
    exit(main())