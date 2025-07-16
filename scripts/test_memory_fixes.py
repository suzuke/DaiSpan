#!/usr/bin/env python3
"""
DaiSpan Memory Leak Fixes Test Script
測試記憶體洩漏修復效果
"""

import requests
import time
import json
import sys
from datetime import datetime

class MemoryFixTester:
    def __init__(self, ip: str = "192.168.50.192"):
        self.ip = ip
        self.web_port = 8080
        self.base_url = f"http://{ip}:{self.web_port}"
        
    def test_memory_api_endpoints(self):
        """測試記憶體相關 API 端點"""
        print("🧪 測試記憶體相關 API 端點...")
        
        endpoints = [
            "/api/health",
            "/api/metrics", 
            "/api/logs",
            "/api/memory/cleanup",
            "/api/core/stats"
        ]
        
        results = {}
        
        for endpoint in endpoints:
            try:
                print(f"  - 測試 {endpoint}...")
                response = requests.get(f"{self.base_url}{endpoint}", timeout=10)
                
                if response.status_code == 200:
                    try:
                        data = response.json()
                        results[endpoint] = {
                            "status": "success",
                            "response_time": response.elapsed.total_seconds(),
                            "memory_info": self._extract_memory_info(data)
                        }
                        print(f"    ✅ 成功 ({response.elapsed.total_seconds():.2f}s)")
                    except json.JSONDecodeError:
                        results[endpoint] = {
                            "status": "success_no_json",
                            "response_time": response.elapsed.total_seconds()
                        }
                        print(f"    ✅ 成功 (非JSON響應)")
                else:
                    results[endpoint] = {
                        "status": "error",
                        "status_code": response.status_code
                    }
                    print(f"    ❌ 失敗 (狀態碼: {response.status_code})")
                    
            except Exception as e:
                results[endpoint] = {
                    "status": "exception",
                    "error": str(e)
                }
                print(f"    ❌ 異常: {e}")
                
        return results
    
    def _extract_memory_info(self, data):
        """提取記憶體資訊"""
        memory_info = {}
        
        if "memory" in data:
            memory_info.update(data["memory"])
        
        if "performance" in data:
            perf = data["performance"]
            memory_info.update({
                "freeHeap": perf.get("freeHeap"),
                "heapSize": perf.get("heapSize"),
                "memoryUsage": perf.get("memoryUsage"),
                "minFreeHeap": perf.get("minFreeHeap"),
                "maxAllocHeap": perf.get("maxAllocHeap")
            })
        
        return memory_info
    
    def test_memory_cleanup_api(self):
        """測試記憶體清理 API"""
        print("🧹 測試記憶體清理 API...")
        
        try:
            # 獲取清理前的記憶體狀態
            before_response = requests.get(f"{self.base_url}/api/metrics", timeout=10)
            before_data = before_response.json()
            before_memory = before_data.get("performance", {}).get("freeHeap", 0)
            
            print(f"  - 清理前可用記憶體: {before_memory} bytes")
            
            # 執行記憶體清理
            cleanup_response = requests.get(f"{self.base_url}/api/memory/cleanup", timeout=10)
            cleanup_data = cleanup_response.json()
            
            if cleanup_response.status_code == 200:
                print(f"  - 清理結果:")
                print(f"    • 清理前: {cleanup_data.get('memoryBefore', 0)} bytes")
                print(f"    • 清理後: {cleanup_data.get('memoryAfter', 0)} bytes")
                print(f"    • 釋放記憶體: {cleanup_data.get('memoryFreed', 0)} bytes")
                
                # 等待一段時間，再次檢查記憶體狀態
                time.sleep(2)
                
                after_response = requests.get(f"{self.base_url}/api/metrics", timeout=10)
                after_data = after_response.json()
                after_memory = after_data.get("performance", {}).get("freeHeap", 0)
                
                print(f"  - 清理後可用記憶體: {after_memory} bytes")
                
                memory_improvement = after_memory - before_memory
                print(f"  - 記憶體改善: {memory_improvement} bytes")
                
                return {
                    "status": "success",
                    "memory_before": before_memory,
                    "memory_after": after_memory,
                    "memory_improvement": memory_improvement,
                    "cleanup_data": cleanup_data
                }
            else:
                print(f"  ❌ 清理失敗 (狀態碼: {cleanup_response.status_code})")
                return {"status": "error", "status_code": cleanup_response.status_code}
                
        except Exception as e:
            print(f"  ❌ 清理測試異常: {e}")
            return {"status": "exception", "error": str(e)}
    
    def test_memory_stress(self, duration_minutes=5):
        """記憶體壓力測試"""
        print(f"📊 記憶體壓力測試 ({duration_minutes} 分鐘)...")
        
        start_time = time.time()
        end_time = start_time + (duration_minutes * 60)
        
        memory_readings = []
        
        while time.time() < end_time:
            try:
                response = requests.get(f"{self.base_url}/api/metrics", timeout=5)
                if response.status_code == 200:
                    data = response.json()
                    memory_info = self._extract_memory_info(data)
                    
                    reading = {
                        "timestamp": datetime.now().isoformat(),
                        "uptime": time.time() - start_time,
                        "free_heap": memory_info.get("freeHeap", 0),
                        "memory_usage": memory_info.get("memoryUsage", 0)
                    }
                    
                    memory_readings.append(reading)
                    
                    print(f"  • 運行時間: {reading['uptime']:.0f}s, "
                          f"可用記憶體: {reading['free_heap']} bytes, "
                          f"使用率: {reading['memory_usage']:.1f}%")
                
                time.sleep(30)  # 每30秒測試一次
                
            except Exception as e:
                print(f"  ⚠️ 測試錯誤: {e}")
                time.sleep(30)
        
        return self._analyze_memory_readings(memory_readings)
    
    def _analyze_memory_readings(self, readings):
        """分析記憶體讀數"""
        if not readings:
            return {"status": "no_data"}
        
        free_heaps = [r["free_heap"] for r in readings if r["free_heap"] > 0]
        
        if not free_heaps:
            return {"status": "invalid_data"}
        
        analysis = {
            "total_readings": len(readings),
            "min_free_heap": min(free_heaps),
            "max_free_heap": max(free_heaps),
            "avg_free_heap": sum(free_heaps) / len(free_heaps),
            "memory_trend": self._calculate_trend(free_heaps),
            "memory_stability": self._calculate_stability(free_heaps)
        }
        
        print(f"\n📈 記憶體分析結果:")
        print(f"  • 總讀數: {analysis['total_readings']}")
        print(f"  • 最小可用記憶體: {analysis['min_free_heap']} bytes")
        print(f"  • 最大可用記憶體: {analysis['max_free_heap']} bytes") 
        print(f"  • 平均可用記憶體: {analysis['avg_free_heap']:.0f} bytes")
        print(f"  • 記憶體趨勢: {analysis['memory_trend']}")
        print(f"  • 記憶體穩定性: {analysis['memory_stability']}")
        
        return analysis
    
    def _calculate_trend(self, values):
        """計算記憶體趨勢"""
        if len(values) < 2:
            return "insufficient_data"
        
        first_half = values[:len(values)//2]
        second_half = values[len(values)//2:]
        
        first_avg = sum(first_half) / len(first_half)
        second_avg = sum(second_half) / len(second_half)
        
        diff = second_avg - first_avg
        
        if abs(diff) < 1000:  # 小於1KB變化
            return "stable"
        elif diff > 0:
            return "improving"
        else:
            return "declining"
    
    def _calculate_stability(self, values):
        """計算記憶體穩定性"""
        if len(values) < 3:
            return "insufficient_data"
        
        avg = sum(values) / len(values)
        variance = sum((x - avg) ** 2 for x in values) / len(values)
        std_dev = variance ** 0.5
        
        stability_ratio = std_dev / avg
        
        if stability_ratio < 0.05:
            return "excellent"
        elif stability_ratio < 0.1:
            return "good"
        elif stability_ratio < 0.2:
            return "moderate"
        else:
            return "poor"
    
    def run_comprehensive_test(self):
        """運行綜合測試"""
        print("🔍 DaiSpan 記憶體洩漏修復驗證測試")
        print("=" * 50)
        
        # 測試 1: API 端點測試
        api_results = self.test_memory_api_endpoints()
        
        print("\n")
        
        # 測試 2: 記憶體清理測試
        cleanup_results = self.test_memory_cleanup_api()
        
        print("\n")
        
        # 測試 3: 記憶體壓力測試
        stress_results = self.test_memory_stress(duration_minutes=3)
        
        print("\n")
        
        # 總結
        self._print_summary(api_results, cleanup_results, stress_results)
        
        return {
            "api_results": api_results,
            "cleanup_results": cleanup_results,
            "stress_results": stress_results
        }
    
    def _print_summary(self, api_results, cleanup_results, stress_results):
        """打印測試總結"""
        print("📋 測試總結:")
        print("-" * 30)
        
        # API 測試總結
        successful_apis = sum(1 for r in api_results.values() if r["status"] == "success")
        print(f"✅ API 端點測試: {successful_apis}/{len(api_results)} 成功")
        
        # 清理測試總結
        if cleanup_results["status"] == "success":
            improvement = cleanup_results.get("memory_improvement", 0)
            print(f"✅ 記憶體清理測試: 成功 (改善 {improvement} bytes)")
        else:
            print(f"❌ 記憶體清理測試: 失敗")
        
        # 壓力測試總結
        if stress_results.get("memory_trend") == "stable":
            print(f"✅ 記憶體壓力測試: 穩定")
        elif stress_results.get("memory_trend") == "improving":
            print(f"✅ 記憶體壓力測試: 改善中")
        else:
            print(f"⚠️ 記憶體壓力測試: {stress_results.get('memory_trend', 'unknown')}")
        
        print("\n💡 建議:")
        if cleanup_results["status"] == "success":
            print("• 記憶體清理功能正常運作")
        else:
            print("• 檢查記憶體清理 API 端點")
        
        if stress_results.get("memory_stability") in ["excellent", "good"]:
            print("• 記憶體使用穩定，洩漏修復有效")
        else:
            print("• 建議繼續監控記憶體使用情況")

if __name__ == "__main__":
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.50.192"
    
    tester = MemoryFixTester(ip)
    results = tester.run_comprehensive_test()
    
    # 輸出結果到文件
    with open("memory_test_results.json", "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    
    print(f"\n📄 詳細結果已保存到 memory_test_results.json")