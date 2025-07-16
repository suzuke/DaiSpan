#!/usr/bin/env python3
"""
DaiSpan HomeKit Pairing Detection Fix Test Script
測試 HomeKit 配對狀態檢測修復效果
"""

import requests
import time
import json
import sys
from datetime import datetime

class PairingFixTester:
    def __init__(self, ip: str = "192.168.50.192"):
        self.ip = ip
        self.web_port = 8080
        self.base_url = f"http://{ip}:{self.web_port}"
        
    def test_pairing_status(self, duration_minutes=10):
        """測試配對狀態檢測邏輯"""
        print(f"🔍 測試 HomeKit 配對狀態檢測 ({duration_minutes} 分鐘)...")
        
        start_time = time.time()
        end_time = start_time + (duration_minutes * 60)
        
        readings = []
        
        while time.time() < end_time:
            try:
                # 獲取健康狀態
                health_response = requests.get(f"{self.base_url}/api/health", timeout=5)
                
                if health_response.status_code == 200:
                    health_data = health_response.json()
                    
                    # 獲取詳細指標
                    metrics_response = requests.get(f"{self.base_url}/api/metrics", timeout=5)
                    metrics_data = metrics_response.json() if metrics_response.status_code == 200 else {}
                    
                    reading = {
                        "timestamp": datetime.now().isoformat(),
                        "uptime": time.time() - start_time,
                        "homekit_initialized": health_data.get("services", {}).get("homekit", False),
                        "memory_free": health_data.get("memory", {}).get("free", 0),
                        "memory_usage": health_data.get("memory", {}).get("usage", 0),
                        "pairing_active": None  # 將從系統日誌中推斷
                    }
                    
                    # 從 HomeKit 指標中獲取配對狀態
                    homekit_info = metrics_data.get("homekit", {})
                    if homekit_info:
                        reading["pairing_active"] = homekit_info.get("pairingActive", False)
                    
                    readings.append(reading)
                    
                    status_icon = "🟢" if reading["homekit_initialized"] else "🔴"
                    pairing_icon = "🔄" if reading.get("pairing_active") else "✅"
                    
                    print(f"  {status_icon} {pairing_icon} 運行時間: {reading['uptime']:.0f}s, "
                          f"HomeKit: {'初始化' if reading['homekit_initialized'] else '未初始化'}, "
                          f"記憶體: {reading['memory_free']} bytes ({reading['memory_usage']:.1f}%), "
                          f"配對中: {'是' if reading.get('pairing_active') else '否'}")
                
                else:
                    print(f"  ❌ API 調用失敗: {health_response.status_code}")
                
                time.sleep(20)  # 每20秒檢查一次
                
            except Exception as e:
                print(f"  ⚠️ 測試錯誤: {e}")
                time.sleep(20)
        
        return self._analyze_pairing_readings(readings)
    
    def _analyze_pairing_readings(self, readings):
        """分析配對狀態讀數"""
        if not readings:
            return {"status": "no_data"}
        
        # 統計配對狀態
        pairing_active_count = sum(1 for r in readings if r.get("pairing_active"))
        total_valid_readings = sum(1 for r in readings if r.get("pairing_active") is not None)
        
        # 記憶體分析
        memory_values = [r["memory_free"] for r in readings if r["memory_free"] > 0]
        
        analysis = {
            "total_readings": len(readings),
            "valid_pairing_readings": total_valid_readings,
            "pairing_active_readings": pairing_active_count,
            "pairing_active_percentage": (pairing_active_count / total_valid_readings * 100) if total_valid_readings > 0 else 0,
            "memory_stats": {
                "min": min(memory_values) if memory_values else 0,
                "max": max(memory_values) if memory_values else 0,
                "avg": sum(memory_values) / len(memory_values) if memory_values else 0,
                "stability": self._calculate_stability(memory_values)
            }
        }
        
        print(f"\n📊 配對狀態分析:")
        print(f"  • 總讀數: {analysis['total_readings']}")
        print(f"  • 有效配對讀數: {analysis['valid_pairing_readings']}")
        print(f"  • 配對活動讀數: {analysis['pairing_active_readings']}")
        print(f"  • 配對活動百分比: {analysis['pairing_active_percentage']:.1f}%")
        print(f"  • 記憶體統計:")
        print(f"    - 最小: {analysis['memory_stats']['min']} bytes")
        print(f"    - 最大: {analysis['memory_stats']['max']} bytes")
        print(f"    - 平均: {analysis['memory_stats']['avg']:.0f} bytes")
        print(f"    - 穩定性: {analysis['memory_stats']['stability']}")
        
        return analysis
    
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
    
    def test_webserver_connectivity(self):
        """測試 WebServer 連接性"""
        print("🌐 測試 WebServer 連接性...")
        
        endpoints = [
            "/api/health",
            "/api/metrics",
            "/api/logs",
            "/api/core/stats"
        ]
        
        results = {}
        
        for endpoint in endpoints:
            try:
                response = requests.get(f"{self.base_url}{endpoint}", timeout=10)
                
                if response.status_code == 200:
                    results[endpoint] = {
                        "status": "success",
                        "response_time": response.elapsed.total_seconds()
                    }
                    print(f"  ✅ {endpoint}: 成功 ({response.elapsed.total_seconds():.2f}s)")
                else:
                    results[endpoint] = {
                        "status": "error",
                        "status_code": response.status_code
                    }
                    print(f"  ❌ {endpoint}: 失敗 (狀態碼: {response.status_code})")
                    
            except Exception as e:
                results[endpoint] = {
                    "status": "exception",
                    "error": str(e)
                }
                print(f"  ❌ {endpoint}: 異常 ({e})")
        
        return results
    
    def run_comprehensive_test(self):
        """運行綜合測試"""
        print("🔍 DaiSpan HomeKit 配對狀態修復驗證測試")
        print("=" * 60)
        
        # 測試 1: WebServer 連接性
        connectivity_results = self.test_webserver_connectivity()
        
        print("\n")
        
        # 測試 2: 配對狀態檢測
        pairing_results = self.test_pairing_status(duration_minutes=5)
        
        print("\n")
        
        # 總結
        self._print_summary(connectivity_results, pairing_results)
        
        return {
            "connectivity_results": connectivity_results,
            "pairing_results": pairing_results
        }
    
    def _print_summary(self, connectivity_results, pairing_results):
        """打印測試總結"""
        print("📋 測試總結:")
        print("-" * 30)
        
        # 連接性測試總結
        successful_endpoints = sum(1 for r in connectivity_results.values() if r["status"] == "success")
        print(f"✅ WebServer 連接性: {successful_endpoints}/{len(connectivity_results)} 端點成功")
        
        # 配對狀態測試總結
        pairing_percentage = pairing_results.get("pairing_active_percentage", 0)
        if pairing_percentage < 10:
            print(f"✅ 配對狀態檢測: 優秀 ({pairing_percentage:.1f}% 誤判)")
        elif pairing_percentage < 25:
            print(f"⚠️ 配對狀態檢測: 良好 ({pairing_percentage:.1f}% 誤判)")
        elif pairing_percentage < 50:
            print(f"⚠️ 配對狀態檢測: 一般 ({pairing_percentage:.1f}% 誤判)")
        else:
            print(f"❌ 配對狀態檢測: 需改進 ({pairing_percentage:.1f}% 誤判)")
        
        # 記憶體穩定性
        memory_stability = pairing_results.get("memory_stats", {}).get("stability", "unknown")
        if memory_stability in ["excellent", "good"]:
            print(f"✅ 記憶體穩定性: {memory_stability}")
        else:
            print(f"⚠️ 記憶體穩定性: {memory_stability}")
        
        print("\n💡 建議:")
        if pairing_percentage < 10:
            print("• 配對狀態檢測修復成功")
        else:
            print("• 考慮進一步優化配對狀態檢測邏輯")
        
        if memory_stability in ["excellent", "good"]:
            print("• 記憶體使用穩定")
        else:
            print("• 建議繼續監控記憶體使用情況")

if __name__ == "__main__":
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.50.192"
    
    tester = PairingFixTester(ip)
    results = tester.run_comprehensive_test()
    
    # 輸出結果到文件
    with open("pairing_fix_test_results.json", "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    
    print(f"\n📄 詳細結果已保存到 pairing_fix_test_results.json")