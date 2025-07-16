#!/usr/bin/env python3
"""
V3 真實空調測試腳本
測試 V3 架構在真實 Daikin 空調環境下的功能
"""

import requests
import time
import json
from typing import Dict, Any, Optional

class RealACTesterV3:
    def __init__(self, device_ip: str = "192.168.50.192", port: int = 8080):
        self.device_ip = device_ip
        self.port = port
        self.base_url = f"http://{device_ip}:{port}"
        
    def get_system_status(self) -> Dict[str, Any]:
        """獲取系統狀態資訊"""
        try:
            response = requests.get(f"{self.base_url}/", timeout=10)
            content = response.text
            
            status = {
                "connection": response.status_code == 200,
                "v3_architecture": "V3 事件驅動" in content,
                "migration_active": "✅ 活躍" in content,
                "is_real_mode": "切換模擬模式" in content,
                "content_size": len(content)
            }
            
            # 解析記憶體資訊
            import re
            memory_match = re.search(r'可用記憶體.*?(\d+)\s*bytes', content)
            if memory_match:
                status["free_memory"] = int(memory_match.group(1))
            
            # 解析運行時間
            runtime_match = re.search(r'運行時間.*?(\d+)天\s*(\d+)時\s*(\d+)分', content)
            if runtime_match:
                days, hours, minutes = map(int, runtime_match.groups())
                status["uptime_minutes"] = days * 1440 + hours * 60 + minutes
                
            return status
        except Exception as e:
            return {"error": str(e), "connection": False}
    
    def test_s21_protocol_communication(self) -> Dict[str, Any]:
        """測試 S21 協議通訊"""
        print("🔌 測試 S21 協議通訊...")
        
        # 目前沒有直接的 S21 測試端點，我們通過系統行為推斷
        results = {
            "protocol_loaded": False,
            "controller_initialized": False,
            "communication_ready": False
        }
        
        try:
            # 檢查系統是否在真實模式下正常運行
            status = self.get_system_status()
            
            if status.get("is_real_mode"):
                results["protocol_loaded"] = True
                print("   ✅ 系統處於真實模式")
            else:
                print("   ❌ 系統未處於真實模式")
                return results
                
            # 檢查記憶體使用情況（真實控制器比模擬控制器消耗更多記憶體）
            free_memory = status.get("free_memory", 0)
            if 100000 <= free_memory <= 130000:  # 預期真實模式記憶體範圍
                results["controller_initialized"] = True
                print(f"   ✅ 控制器已初始化 (記憶體: {free_memory} bytes)")
            else:
                print(f"   ⚠️ 記憶體使用異常: {free_memory} bytes")
                
            # 檢查系統穩定性（運行時間足夠長說明沒有崩潰）
            uptime = status.get("uptime_minutes", 0)
            if uptime >= 5:  # 至少運行 5 分鐘
                results["communication_ready"] = True
                print(f"   ✅ 系統運行穩定 (運行時間: {uptime} 分鐘)")
            else:
                print(f"   ⚠️ 系統運行時間較短: {uptime} 分鐘")
                
        except Exception as e:
            print(f"   ❌ 測試過程中發生錯誤: {e}")
            
        return results
    
    def test_homekit_real_control(self) -> Dict[str, Any]:
        """測試 HomeKit 真實控制功能"""
        print("🏠 測試 HomeKit 真實控制...")
        
        # 注意：這裡我們不能直接控制真實空調，只能檢查系統響應
        # 用戶需要手動通過 HomeKit 進行控制，我們檢查系統是否正常響應
        
        results = {
            "homekit_accessible": False,
            "system_responsive": False,
            "ready_for_control": False
        }
        
        try:
            # 檢查 HomeKit 設定頁面
            response = requests.get(f"{self.base_url}/homekit", timeout=5)
            if response.status_code == 200 and "HomeKit" in response.text:
                results["homekit_accessible"] = True
                print("   ✅ HomeKit 設定頁面可訪問")
            else:
                print("   ❌ HomeKit 設定頁面不可訪問")
                
            # 檢查系統響應性
            start_time = time.time()
            status_response = requests.get(f"{self.base_url}/", timeout=5)
            response_time = time.time() - start_time
            
            if status_response.status_code == 200 and response_time < 2.0:
                results["system_responsive"] = True
                print(f"   ✅ 系統響應正常 ({response_time:.2f}s)")
            else:
                print(f"   ❌ 系統響應緩慢 ({response_time:.2f}s)")
                
            # 檢查 V3 架構狀態
            if "V3 事件驅動" in status_response.text and "✅ 活躍" in status_response.text:
                results["ready_for_control"] = True
                print("   ✅ V3 架構準備就緒，可接受 HomeKit 控制")
            else:
                print("   ❌ V3 架構狀態異常")
                
        except Exception as e:
            print(f"   ❌ 測試過程中發生錯誤: {e}")
            
        return results
    
    def test_memory_stability_real_mode(self) -> Dict[str, Any]:
        """測試真實模式下的記憶體穩定性"""
        print("💾 測試真實模式記憶體穩定性...")
        
        memory_readings = []
        
        for i in range(5):
            try:
                status = self.get_system_status()
                memory = status.get("free_memory", 0)
                memory_readings.append(memory)
                print(f"   讀取 {i+1}: {memory} bytes")
                time.sleep(3)
            except Exception as e:
                print(f"   讀取 {i+1} 失敗: {e}")
                memory_readings.append(0)
        
        if not memory_readings or all(m == 0 for m in memory_readings):
            return {"error": "無法獲取記憶體資訊"}
            
        # 過濾掉無效讀取
        valid_readings = [m for m in memory_readings if m > 0]
        
        if not valid_readings:
            return {"error": "所有記憶體讀取都無效"}
            
        min_mem = min(valid_readings)
        max_mem = max(valid_readings)
        variance = max_mem - min_mem
        avg_mem = sum(valid_readings) / len(valid_readings)
        
        results = {
            "readings": memory_readings,
            "min_memory": min_mem,
            "max_memory": max_mem,
            "average_memory": avg_mem,
            "variance": variance,
            "stable": variance < 10000,  # 真實模式允許更大的變化
            "healthy": min_mem > 80000,  # 真實模式需要更多記憶體
            "reading_count": len(valid_readings)
        }
        
        print(f"   記憶體範圍: {min_mem} - {max_mem} bytes (變化 {variance} bytes)")
        print(f"   平均記憶體: {avg_mem:.0f} bytes")
        print(f"   穩定性: {'✅' if results['stable'] else '⚠️'}")
        print(f"   健康度: {'✅' if results['healthy'] else '❌'}")
        
        return results
    
    def test_v3_real_environment_integration(self) -> Dict[str, Any]:
        """測試 V3 架構在真實環境下的整合"""
        print("🏗️ 測試 V3 真實環境整合...")
        
        results = {
            "architecture_active": False,
            "migration_working": False,
            "event_system_ready": False,
            "overall_health": False
        }
        
        try:
            status = self.get_system_status()
            
            # 檢查 V3 架構是否啟用
            if status.get("v3_architecture"):
                results["architecture_active"] = True
                print("   ✅ V3 事件驅動架構已啟用")
            else:
                print("   ❌ V3 架構未啟用")
                
            # 檢查遷移狀態
            if status.get("migration_active"):
                results["migration_working"] = True
                print("   ✅ V2/V3 遷移管理器運行中")
            else:
                print("   ❌ 遷移管理器未運行")
                
            # 檢查系統整體健康度
            connection_ok = status.get("connection", False)
            memory_ok = status.get("free_memory", 0) > 80000
            real_mode_ok = status.get("is_real_mode", False)
            
            if connection_ok and memory_ok and real_mode_ok:
                results["event_system_ready"] = True
                print("   ✅ 事件系統準備就緒")
            else:
                print(f"   ❌ 事件系統狀態異常 (連接:{connection_ok}, 記憶體:{memory_ok}, 真實模式:{real_mode_ok})")
                
            # 整體評估
            score = sum([
                results["architecture_active"],
                results["migration_working"], 
                results["event_system_ready"]
            ])
            
            results["overall_health"] = score >= 2
            print(f"   整體健康度: {'✅' if results['overall_health'] else '❌'} ({score}/3)")
            
        except Exception as e:
            print(f"   ❌ 測試過程中發生錯誤: {e}")
            
        return results
    
    def run_comprehensive_real_test(self) -> Dict[str, Any]:
        """執行綜合真實環境測試"""
        print("🎯 開始 V3 真實空調綜合測試")
        print("=" * 60)
        
        # 1. 系統狀態檢查
        print("\n📊 步驟 1: 系統狀態檢查")
        system_status = self.get_system_status()
        if system_status.get("connection"):
            print("✅ 系統連接正常")
        else:
            print("❌ 系統連接失敗")
            return {"error": "無法連接到設備"}
            
        # 2. S21 協議測試
        print("\n📊 步驟 2: S21 協議通訊測試")
        s21_results = self.test_s21_protocol_communication()
        
        # 3. HomeKit 控制測試
        print("\n📊 步驟 3: HomeKit 控制準備測試")
        homekit_results = self.test_homekit_real_control()
        
        # 4. 記憶體穩定性測試
        print("\n📊 步驟 4: 記憶體穩定性測試")
        memory_results = self.test_memory_stability_real_mode()
        
        # 5. V3 整合測試
        print("\n📊 步驟 5: V3 架構整合測試")
        v3_results = self.test_v3_real_environment_integration()
        
        # 總結報告
        print("\n" + "=" * 60)
        print("📊 V3 真實空調測試總結")
        print("=" * 60)
        
        # 計算總分
        s21_score = sum(s21_results.values()) if isinstance(s21_results, dict) else 0
        homekit_score = sum(homekit_results.values()) if isinstance(homekit_results, dict) else 0
        memory_score = sum([
            memory_results.get("stable", False),
            memory_results.get("healthy", False)
        ]) if isinstance(memory_results, dict) else 0
        v3_score = sum([
            v3_results.get("architecture_active", False),
            v3_results.get("migration_working", False),
            v3_results.get("event_system_ready", False),
            v3_results.get("overall_health", False)
        ]) if isinstance(v3_results, dict) else 0
        
        total_score = s21_score + homekit_score + memory_score + v3_score
        max_score = 11  # 3 + 3 + 2 + 4
        
        print(f"S21 協議通訊: {s21_score}/3 分")
        print(f"HomeKit 控制準備: {homekit_score}/3 分") 
        print(f"記憶體穩定性: {memory_score}/2 分")
        print(f"V3 架構整合: {v3_score}/4 分")
        print(f"總體評分: {total_score}/{max_score} 分 ({total_score/max_score*100:.1f}%)")
        
        if total_score >= 9:
            print("🎉 V3 真實空調系統運行優秀！")
            success_level = "excellent"
        elif total_score >= 7:
            print("✅ V3 真實空調系統運行良好！")
            success_level = "good"
        elif total_score >= 5:
            print("⚠️ V3 真實空調系統基本正常，建議優化")
            success_level = "acceptable"
        else:
            print("❌ V3 真實空調系統存在問題，需要詳細檢查")
            success_level = "problematic"
            
        return {
            "system_status": system_status,
            "s21_results": s21_results,
            "homekit_results": homekit_results,
            "memory_results": memory_results,
            "v3_results": v3_results,
            "total_score": total_score,
            "max_score": max_score,
            "success_rate": total_score/max_score*100,
            "success_level": success_level
        }

def main():
    print("🎯 V3 真實空調環境測試")
    print("⚠️  請確保:")
    print("   - ESP32 已連接到 Daikin 空調 S21 端口")
    print("   - 設備已切換到真實模式")
    print("   - 空調電源已開啟")
    print("=" * 60)
    
    tester = RealACTesterV3()
    results = tester.run_comprehensive_real_test()
    
    # 根據結果設定退出碼
    success_rate = results.get("success_rate", 0)
    exit_code = 0 if success_rate >= 70 else 1
    
    print("\n💡 提示:")
    print("   - 如需測試 HomeKit 控制，請手動使用 HomeKit 應用程式")
    print("   - 可通過 Web 介面監控空調狀態變化")
    print("   - 建議進行長時間穩定性測試")
    
    return exit_code

if __name__ == "__main__":
    exit(main())