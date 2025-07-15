#!/usr/bin/env python3
"""
V3 事件系統專項測試
測試事件發布、訂閱和處理功能
"""

import requests
import time
import json
from typing import Dict, Any

class V3EventSystemTester:
    def __init__(self, device_ip: str, port: int = 8080):
        self.base_url = f"http://{device_ip}:{port}"
        self.device_ip = device_ip
        
    def get_baseline_stats(self) -> Dict[str, Any]:
        """獲取基準統計資料"""
        try:
            response = requests.get(f"{self.base_url}/", timeout=5)
            content = response.text
            
            # 解析事件統計
            stats = {"queue": 0, "subscriptions": 0, "processed": 0}
            if "事件統計:" in content:
                stats_line = content.split("事件統計:")[1].split("</p>")[0]
                print(f"📊 事件統計原始資料: {stats_line}")
                
                # 解析佇列、訂閱、已處理
                if "佇列:" in stats_line:
                    queue_part = stats_line.split("佇列:")[1].split(" ")[0]
                    try:
                        stats["queue"] = int(queue_part)
                    except ValueError:
                        pass
                        
            # 解析記憶體使用 - 修正解析邏輯
            if "可用記憶體:" in content:
                memory_section = content.split("可用記憶體:")[1].split("bytes")[0]
                # 尋找數字部分
                import re
                memory_match = re.search(r'>(\d+)', memory_section)
                if memory_match:
                    stats["memory"] = int(memory_match.group(1))
                else:
                    stats["memory"] = 0
                    
            return stats
        except Exception as e:
            print(f"❌ 獲取基準統計失敗: {e}")
            return {"queue": 0, "subscriptions": 0, "processed": 0, "memory": 0}
    
    def trigger_thermostat_commands(self, count: int = 5) -> bool:
        """觸發多個恆溫器命令來產生事件"""
        success_count = 0
        
        commands = [
            {"power": "1", "mode": "2", "target_temp": "20.0"},  # 開機制冷
            {"power": "1", "mode": "1", "target_temp": "24.0"},  # 制熱
            {"power": "1", "mode": "3", "target_temp": "22.0"},  # 自動
            {"power": "1", "fan_speed": "5", "target_temp": "23.0"},  # 高風速
            {"power": "0", "mode": "0", "target_temp": "21.0"},  # 關機
        ]
        
        for i, cmd in enumerate(commands[:count]):
            try:
                print(f"🔄 發送命令 {i+1}/{count}: {cmd}")
                
                # 添加基本參數
                data = {
                    "current_temp": "25.0",
                    "room_temp": "25.0",
                    **cmd
                }
                
                response = requests.post(
                    f"{self.base_url}/simulation-control",
                    data=data,
                    timeout=5
                )
                
                if response.status_code == 200:
                    success_count += 1
                    print(f"   ✅ 命令 {i+1} 成功")
                else:
                    print(f"   ❌ 命令 {i+1} 失敗: HTTP {response.status_code}")
                    
                time.sleep(1)  # 給事件系統處理時間
                
            except Exception as e:
                print(f"   💥 命令 {i+1} 錯誤: {e}")
        
        return success_count == count
    
    def test_event_system_activation(self) -> Dict[str, Any]:
        """測試事件系統激活"""
        print("🧪 測試 V3 事件系統激活...")
        
        # 1. 獲取基準統計
        print("\n📊 步驟 1: 獲取基準統計")
        baseline = self.get_baseline_stats()
        print(f"   基準狀態: 佇列={baseline['queue']}, 記憶體={baseline.get('memory', 0)} bytes")
        
        # 2. 觸發事件
        print("\n🚀 步驟 2: 觸發恆溫器控制事件")
        commands_success = self.trigger_thermostat_commands(3)
        
        # 3. 等待事件處理
        print("\n⏳ 步驟 3: 等待事件處理...")
        time.sleep(3)
        
        # 4. 獲取更新後的統計
        print("\n📈 步驟 4: 檢查事件處理結果")
        after_stats = self.get_baseline_stats()
        print(f"   處理後狀態: 佇列={after_stats['queue']}, 記憶體={after_stats.get('memory', 0)} bytes")
        
        # 5. 分析結果
        memory_changed = abs(baseline.get('memory', 0) - after_stats.get('memory', 0)) > 1000
        queue_activity = after_stats['queue'] != baseline['queue']
        
        results = {
            "commands_sent": commands_success,
            "memory_changed": memory_changed,
            "queue_activity": queue_activity,
            "baseline": baseline,
            "after_stats": after_stats,
            "memory_delta": after_stats.get('memory', 0) - baseline.get('memory', 0)
        }
        
        # 6. 輸出結果
        print(f"\n📋 測試結果:")
        print(f"   命令發送: {'✅' if commands_success else '❌'}")
        print(f"   記憶體變化: {'✅' if memory_changed else '❌'} (Δ{results['memory_delta']} bytes)")
        print(f"   佇列活動: {'✅' if queue_activity else '❌'}")
        
        overall_success = commands_success and (memory_changed or queue_activity)
        print(f"   總體評估: {'✅ 事件系統運作中' if overall_success else '⚠️ 需要檢查'}")
        
        return results
    
    def test_memory_stability(self) -> Dict[str, Any]:
        """測試記憶體穩定性"""
        print("\n💾 測試記憶體穩定性...")
        
        memory_readings = []
        for i in range(5):
            stats = self.get_baseline_stats()
            memory = stats.get('memory', 0)
            memory_readings.append(memory)
            print(f"   讀取 {i+1}: {memory} bytes")
            time.sleep(2)
        
        # 分析記憶體穩定性
        min_mem = min(memory_readings)
        max_mem = max(memory_readings)
        variance = max_mem - min_mem
        avg_mem = sum(memory_readings) / len(memory_readings)
        
        # 記憶體變化超過 5KB 視為不穩定
        stable = variance < 5000
        healthy = min_mem > 100000  # 至少 100KB 可用記憶體
        
        results = {
            "readings": memory_readings,
            "min_memory": min_mem,
            "max_memory": max_mem,
            "average_memory": avg_mem,
            "variance": variance,
            "stable": stable,
            "healthy": healthy
        }
        
        print(f"   記憶體範圍: {min_mem} - {max_mem} bytes (變化 {variance} bytes)")
        print(f"   平均記憶體: {avg_mem:.0f} bytes")
        print(f"   穩定性: {'✅' if stable else '⚠️'}")
        print(f"   健康度: {'✅' if healthy else '❌'}")
        
        return results

def main():
    import sys
    device_ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.50.192"
    
    print(f"🎯 V3 事件系統測試目標: {device_ip}:8080")
    print("=" * 60)
    
    tester = V3EventSystemTester(device_ip)
    
    # 測試事件系統激活
    event_results = tester.test_event_system_activation()
    
    # 測試記憶體穩定性
    memory_results = tester.test_memory_stability()
    
    # 總結報告
    print("\n" + "=" * 60)
    print("📊 V3 事件系統測試總結")
    print("=" * 60)
    
    event_score = sum([
        event_results["commands_sent"],
        event_results["memory_changed"],
        event_results["queue_activity"]
    ])
    
    memory_score = sum([
        memory_results["stable"],
        memory_results["healthy"]
    ])
    
    total_score = event_score + memory_score
    max_score = 5
    
    print(f"事件系統功能: {event_score}/3 分")
    print(f"記憶體穩定性: {memory_score}/2 分")
    print(f"總體評分: {total_score}/{max_score} 分 ({total_score/max_score*100:.1f}%)")
    
    if total_score >= 4:
        print("🎉 V3 事件系統運作良好！")
        return 0
    elif total_score >= 2:
        print("⚠️ V3 事件系統部分功能正常，建議進一步檢查")
        return 0
    else:
        print("❌ V3 事件系統可能存在問題，需要詳細調試")
        return 1

if __name__ == "__main__":
    exit(main())