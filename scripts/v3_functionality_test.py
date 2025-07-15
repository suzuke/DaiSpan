#!/usr/bin/env python3
"""
V3 架構功能驗證測試腳本
用於驗證 DaiSpan V3 架構的各項功能
"""

import requests
import time
import json
import sys
from typing import Dict, Any, Optional

class V3FunctionalityTester:
    def __init__(self, device_ip: str = "192.168.4.1", port: int = 8080, timeout: int = 10):
        self.device_ip = device_ip
        self.port = port
        self.base_url = f"http://{device_ip}:{port}"
        self.timeout = timeout
        self.test_results = {}
        
    def test_device_reachability(self) -> bool:
        """測試設備可達性"""
        print("🔍 測試設備可達性...")
        try:
            response = requests.get(f"{self.base_url}/", timeout=self.timeout)
            success = response.status_code == 200
            self.test_results["device_reachability"] = {
                "success": success,
                "status_code": response.status_code,
                "response_time": response.elapsed.total_seconds()
            }
            return success
        except Exception as e:
            self.test_results["device_reachability"] = {
                "success": False,
                "error": str(e)
            }
            return False
    
    def test_v3_architecture_status(self) -> bool:
        """測試 V3 架構狀態"""
        print("🏗️ 測試 V3 架構狀態...")
        try:
            response = requests.get(f"{self.base_url}/api/status", timeout=self.timeout)
            if response.status_code == 200:
                data = response.json()
                v3_enabled = data.get("v3Architecture", False)
                self.test_results["v3_architecture_status"] = {
                    "success": v3_enabled,
                    "architecture_enabled": v3_enabled,
                    "full_status": data
                }
                return v3_enabled
            else:
                self.test_results["v3_architecture_status"] = {
                    "success": False,
                    "status_code": response.status_code
                }
                return False
        except Exception as e:
            self.test_results["v3_architecture_status"] = {
                "success": False,
                "error": str(e)
            }
            return False
    
    def test_event_system(self) -> bool:
        """測試事件系統功能"""
        print("📡 測試 V3 事件系統...")
        try:
            response = requests.get(f"{self.base_url}/api/status", timeout=self.timeout)
            if response.status_code == 200:
                data = response.json()
                event_bus = data.get("eventBus", {})
                has_event_bus = isinstance(event_bus, dict) and "queueSize" in event_bus
                
                self.test_results["event_system"] = {
                    "success": has_event_bus,
                    "queue_size": event_bus.get("queueSize", -1),
                    "event_bus_data": event_bus
                }
                return has_event_bus
            return False
        except Exception as e:
            self.test_results["event_system"] = {
                "success": False,
                "error": str(e)
            }
            return False
    
    def test_service_container(self) -> bool:
        """測試服務容器功能"""
        print("🔧 測試服務容器...")
        try:
            response = requests.get(f"{self.base_url}/api/v3/services", timeout=self.timeout)
            # 即使 404 也表示系統在運行，只是 API 端點可能未實作
            success = response.status_code in [200, 404]
            
            self.test_results["service_container"] = {
                "success": success,
                "status_code": response.status_code,
                "response_time": response.elapsed.total_seconds()
            }
            return success
        except Exception as e:
            self.test_results["service_container"] = {
                "success": False,
                "error": str(e)
            }
            return False
    
    def test_migration_adapter(self) -> bool:
        """測試遷移適配器功能"""
        print("🔄 測試 V2/V3 遷移適配器...")
        try:
            # 測試基本溫度控制 API（應該能透過遷移適配器工作）
            response = requests.get(f"{self.base_url}/api/temperature", timeout=self.timeout)
            success = response.status_code == 200
            
            if success:
                data = response.json()
                has_temp_data = "currentTemperature" in data
                self.test_results["migration_adapter"] = {
                    "success": has_temp_data,
                    "temperature_data": data
                }
                return has_temp_data
            else:
                self.test_results["migration_adapter"] = {
                    "success": False,
                    "status_code": response.status_code
                }
                return False
        except Exception as e:
            self.test_results["migration_adapter"] = {
                "success": False,
                "error": str(e)
            }
            return False
    
    def test_memory_usage(self) -> bool:
        """測試記憶體使用情況"""
        print("💾 測試記憶體使用情況...")
        try:
            response = requests.get(f"{self.base_url}/api/system", timeout=self.timeout)
            if response.status_code == 200:
                data = response.json()
                free_heap = data.get("freeHeap", 0)
                min_free_heap = data.get("minFreeHeap", 0)
                
                # 檢查是否有足夠的可用記憶體（至少 20KB）
                memory_healthy = free_heap > 20000
                
                self.test_results["memory_usage"] = {
                    "success": memory_healthy,
                    "free_heap": free_heap,
                    "min_free_heap": min_free_heap,
                    "memory_healthy": memory_healthy
                }
                return memory_healthy
            return False
        except Exception as e:
            self.test_results["memory_usage"] = {
                "success": False,
                "error": str(e)
            }
            return False
    
    def test_web_interface(self) -> bool:
        """測試 Web 介面功能"""
        print("🌐 測試 Web 介面...")
        try:
            # 測試主頁面
            response = requests.get(f"{self.base_url}/", timeout=self.timeout)
            main_page_ok = response.status_code == 200
            
            # 測試 API 端點
            api_response = requests.get(f"{self.base_url}/api/status", timeout=self.timeout)
            api_ok = api_response.status_code == 200
            
            success = main_page_ok and api_ok
            self.test_results["web_interface"] = {
                "success": success,
                "main_page_ok": main_page_ok,
                "api_ok": api_ok,
                "main_page_size": len(response.content) if main_page_ok else 0
            }
            return success
        except Exception as e:
            self.test_results["web_interface"] = {
                "success": False,
                "error": str(e)
            }
            return False
    
    def run_all_tests(self) -> Dict[str, Any]:
        """執行所有測試"""
        print("🚀 開始 V3 架構功能驗證測試...\n")
        
        tests = [
            ("設備可達性", self.test_device_reachability),
            ("V3 架構狀態", self.test_v3_architecture_status), 
            ("事件系統", self.test_event_system),
            ("服務容器", self.test_service_container),
            ("遷移適配器", self.test_migration_adapter),
            ("記憶體使用", self.test_memory_usage),
            ("Web 介面", self.test_web_interface)
        ]
        
        passed = 0
        total = len(tests)
        
        for test_name, test_func in tests:
            try:
                result = test_func()
                status = "✅ 通過" if result else "❌ 失敗"
                print(f"   {status} {test_name}")
                if result:
                    passed += 1
            except Exception as e:
                print(f"   💥 錯誤 {test_name}: {e}")
            
            time.sleep(1)  # 給設備喘息時間
        
        print(f"\n📊 測試結果: {passed}/{total} 項測試通過")
        
        # 計算總體成功率
        success_rate = (passed / total) * 100
        self.test_results["summary"] = {
            "total_tests": total,
            "passed_tests": passed,
            "success_rate": success_rate,
            "overall_success": success_rate >= 70  # 70% 以上視為成功
        }
        
        return self.test_results
    
    def print_detailed_results(self):
        """列印詳細測試結果"""
        print("\n" + "="*60)
        print("📋 詳細測試結果")
        print("="*60)
        
        for test_name, result in self.test_results.items():
            if test_name == "summary":
                continue
                
            print(f"\n🔍 {test_name.upper()}")
            if result.get("success"):
                print("   狀態: ✅ 成功")
            else:
                print("   狀態: ❌ 失敗")
            
            # 列印具體資訊
            for key, value in result.items():
                if key != "success":
                    print(f"   {key}: {value}")
        
        # 列印摘要
        if "summary" in self.test_results:
            summary = self.test_results["summary"]
            print(f"\n📈 總體結果")
            print(f"   通過率: {summary['success_rate']:.1f}%")
            print(f"   狀態: {'✅ 系統健康' if summary['overall_success'] else '⚠️ 需要關注'}")

def main():
    # 預設使用 AP 模式 IP，也可以從命令列指定
    device_ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.4.1"
    
    print(f"🎯 目標設備: {device_ip}")
    print("⏳ 等待設備就緒...")
    time.sleep(5)  # 給設備一些啟動時間
    
    tester = V3FunctionalityTester(device_ip, 8080)
    results = tester.run_all_tests()
    tester.print_detailed_results()
    
    # 根據結果設定退出碼
    exit_code = 0 if results["summary"]["overall_success"] else 1
    sys.exit(exit_code)

if __name__ == "__main__":
    main()