#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32-C3 DaiSpan 長期穩定性測試腳本
功能：
1. 定期檢查設備狀態
2. 監控記憶體使用情況
3. 測試網頁回應
4. 記錄異常情況
5. 生成測試報告
"""

import requests
import time
import datetime
import json
import os
import sys
from typing import Dict, List, Optional
import subprocess
import threading

class DaiSpanMonitor:
    def __init__(self, device_ip: str = "192.168.4.1", port: int = 80):
        self.device_ip = device_ip
        self.port = port
        self.base_url = f"http://{device_ip}:{port}"
        self.test_start_time = datetime.datetime.now()
        self.log_file = f"daispan_test_{self.test_start_time.strftime('%Y%m%d_%H%M%S')}.log"
        self.results = {
            "start_time": self.test_start_time.isoformat(),
            "tests": [],
            "summary": {
                "total_checks": 0,
                "successful_checks": 0,
                "failed_checks": 0,
                "memory_readings": [],
                "response_times": [],
                "errors": []
            }
        }
        
        # 測試頁面列表
        self.test_pages = {
            "/": "主頁",
            "/wifi": "WiFi設定頁",
            "/homekit": "HomeKit設定頁", 
            "/simulation-toggle": "模擬切換頁",
            "/ota": "OTA更新頁"
        }
        
        self.log_message("=== DaiSpan 長期穩定性測試開始 ===")
        self.log_message(f"設備IP: {self.base_url}")
        self.log_message(f"測試開始時間: {self.test_start_time}")

    def log_message(self, message: str, level: str = "INFO"):
        """記錄訊息到檔案和控制台"""
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        formatted_message = f"[{timestamp}] {level}: {message}"
        
        print(formatted_message)
        
        with open(self.log_file, "a", encoding="utf-8") as f:
            f.write(formatted_message + "\n")

    def test_page_response(self, path: str, timeout: int = 10) -> Dict:
        """測試單個頁面回應"""
        url = self.base_url + path
        start_time = time.time()
        
        try:
            response = requests.get(url, timeout=timeout)
            response_time = time.time() - start_time
            
            result = {
                "url": url,
                "status_code": response.status_code,
                "response_time": response_time,
                "content_length": len(response.content),
                "success": response.status_code == 200,
                "timestamp": datetime.datetime.now().isoformat()
            }
            
            if response.status_code == 200:
                # 檢查頁面內容是否包含基本元素
                content = response.text.lower()
                if "daispan" in content or "thermostat" in content or "homekit" in content:
                    result["content_valid"] = True
                else:
                    result["content_valid"] = False
                    result["warning"] = "頁面內容可能異常"
            
            return result
            
        except requests.exceptions.Timeout:
            return {
                "url": url,
                "success": False,
                "error": "請求超時",
                "response_time": timeout,
                "timestamp": datetime.datetime.now().isoformat()
            }
        except requests.exceptions.ConnectionError:
            return {
                "url": url,
                "success": False,
                "error": "連接失敗",
                "response_time": time.time() - start_time,
                "timestamp": datetime.datetime.now().isoformat()
            }
        except Exception as e:
            return {
                "url": url,
                "success": False,
                "error": str(e),
                "response_time": time.time() - start_time,
                "timestamp": datetime.datetime.now().isoformat()
            }

    def extract_memory_info(self, html_content: str) -> Optional[Dict]:
        """從狀態頁面提取記憶體資訊"""
        try:
            # 查找記憶體相關資訊
            lines = html_content.split('\n')
            memory_info = {}
            
            for line in lines:
                if '可用記憶體' in line or 'Free Heap' in line:
                    # 提取數字
                    import re
                    numbers = re.findall(r'\d+', line)
                    if numbers:
                        memory_info['free_heap'] = int(numbers[0])
                
                if '最小可用記憶體' in line or 'Min Free Heap' in line:
                    numbers = re.findall(r'\d+', line)
                    if numbers:
                        memory_info['min_free_heap'] = int(numbers[0])
                        
                if 'Flash 使用' in line or 'Flash Usage' in line:
                    numbers = re.findall(r'\d+', line)
                    if numbers:
                        memory_info['flash_used'] = int(numbers[0])
            
            return memory_info if memory_info else None
            
        except Exception as e:
            self.log_message(f"記憶體資訊解析失敗: {e}", "ERROR")
            return None

    def run_single_test_cycle(self) -> Dict:
        """執行一次完整測試循環"""
        cycle_start = datetime.datetime.now()
        self.log_message(f"開始測試循環 #{self.results['summary']['total_checks'] + 1}")
        
        cycle_results = {
            "cycle_number": self.results['summary']['total_checks'] + 1,
            "start_time": cycle_start.isoformat(),
            "page_tests": [],
            "memory_info": None,
            "overall_success": True
        }
        
        # 測試所有頁面
        for path, description in self.test_pages.items():
            self.log_message(f"測試 {description} ({path})")
            result = self.test_page_response(path)
            cycle_results["page_tests"].append(result)
            
            if result["success"]:
                self.log_message(f"✓ {description} 回應正常 ({result['response_time']:.2f}s)")
                self.results['summary']['response_times'].append(result['response_time'])
                
                # 如果是主頁，嘗試提取記憶體資訊
                if path == "/" and 'content_length' in result:
                    try:
                        response = requests.get(self.base_url + path, timeout=10)
                        memory_info = self.extract_memory_info(response.text)
                        if memory_info:
                            cycle_results["memory_info"] = memory_info
                            self.results['summary']['memory_readings'].append({
                                "timestamp": cycle_start.isoformat(),
                                **memory_info
                            })
                            self.log_message(f"記憶體狀態: {memory_info}")
                    except:
                        pass
            else:
                self.log_message(f"✗ {description} 測試失敗: {result.get('error', '未知錯誤')}", "ERROR")
                cycle_results["overall_success"] = False
                self.results['summary']['errors'].append({
                    "timestamp": cycle_start.isoformat(),
                    "page": path,
                    "error": result.get('error', '未知錯誤')
                })
        
        # 更新統計
        self.results['summary']['total_checks'] += 1
        if cycle_results["overall_success"]:
            self.results['summary']['successful_checks'] += 1
        else:
            self.results['summary']['failed_checks'] += 1
        
        cycle_results["end_time"] = datetime.datetime.now().isoformat()
        cycle_results["duration"] = (datetime.datetime.now() - cycle_start).total_seconds()
        
        self.results["tests"].append(cycle_results)
        
        self.log_message(f"測試循環完成，耗時 {cycle_results['duration']:.2f}s")
        return cycle_results

    def generate_report(self):
        """生成測試報告"""
        now = datetime.datetime.now()
        total_duration = now - self.test_start_time
        
        self.results["end_time"] = now.isoformat()
        self.results["total_duration_seconds"] = total_duration.total_seconds()
        
        # 計算統計資料
        if self.results['summary']['response_times']:
            avg_response_time = sum(self.results['summary']['response_times']) / len(self.results['summary']['response_times'])
            self.results['summary']['avg_response_time'] = avg_response_time
        
        success_rate = 0
        if self.results['summary']['total_checks'] > 0:
            success_rate = (self.results['summary']['successful_checks'] / self.results['summary']['total_checks']) * 100
        self.results['summary']['success_rate'] = success_rate
        
        # 儲存JSON報告
        report_file = f"daispan_report_{self.test_start_time.strftime('%Y%m%d_%H%M%S')}.json"
        with open(report_file, "w", encoding="utf-8") as f:
            json.dump(self.results, f, indent=2, ensure_ascii=False)
        
        # 生成文字報告
        self.log_message("=== 測試報告 ===")
        self.log_message(f"測試總時長: {total_duration}")
        self.log_message(f"總測試循環: {self.results['summary']['total_checks']}")
        self.log_message(f"成功次數: {self.results['summary']['successful_checks']}")
        self.log_message(f"失敗次數: {self.results['summary']['failed_checks']}")
        self.log_message(f"成功率: {success_rate:.1f}%")
        
        if self.results['summary']['response_times']:
            self.log_message(f"平均回應時間: {avg_response_time:.2f}s")
        
        if self.results['summary']['memory_readings']:
            latest_memory = self.results['summary']['memory_readings'][-1]
            self.log_message(f"最新記憶體狀態: {latest_memory}")
        
        if self.results['summary']['errors']:
            self.log_message(f"錯誤總數: {len(self.results['summary']['errors'])}")
            for error in self.results['summary']['errors'][-5:]:  # 顯示最後5個錯誤
                self.log_message(f"  - {error['timestamp']}: {error['page']} - {error['error']}")
        
        self.log_message(f"詳細報告儲存至: {report_file}")
        return report_file

    def run_continuous_test(self, duration_hours: float = 24, interval_minutes: float = 5):
        """執行連續測試"""
        total_duration = duration_hours * 3600  # 轉換為秒
        interval = interval_minutes * 60  # 轉換為秒
        
        self.log_message(f"開始 {duration_hours} 小時連續測試，每 {interval_minutes} 分鐘測試一次")
        
        end_time = time.time() + total_duration
        
        try:
            while time.time() < end_time:
                # 執行測試循環
                self.run_single_test_cycle()
                
                # 每10次測試生成一次中間報告
                if self.results['summary']['total_checks'] % 10 == 0:
                    self.generate_report()
                
                # 計算剩餘時間
                remaining = end_time - time.time()
                if remaining <= 0:
                    break
                
                # 等待下次測試，但不超過剩餘時間
                sleep_time = min(interval, remaining)
                self.log_message(f"等待 {sleep_time/60:.1f} 分鐘後進行下次測試...")
                time.sleep(sleep_time)
                
        except KeyboardInterrupt:
            self.log_message("使用者中斷測試", "WARNING")
        except Exception as e:
            self.log_message(f"測試過程發生錯誤: {e}", "ERROR")
        finally:
            self.log_message("=== 測試結束 ===")
            report_file = self.generate_report()
            return report_file

def main():
    """主函數"""
    if len(sys.argv) < 2:
        print("使用方法:")
        print("  python3 long_term_test.py <device_ip> [duration_hours] [interval_minutes]")
        print("  python3 long_term_test.py single <device_ip>  # 執行單次測試")
        print()
        print("範例:")
        print("  python3 long_term_test.py 192.168.4.1 24 5     # 24小時測試，每5分鐘一次")
        print("  python3 long_term_test.py single 192.168.4.1   # 單次測試")
        return
    
    if sys.argv[1] == "single":
        # 單次測試模式
        device_ip = sys.argv[2] if len(sys.argv) > 2 else "192.168.4.1"
        monitor = DaiSpanMonitor(device_ip)
        monitor.run_single_test_cycle()
        monitor.generate_report()
    else:
        # 連續測試模式
        device_ip = sys.argv[1]
        duration_hours = float(sys.argv[2]) if len(sys.argv) > 2 else 24
        interval_minutes = float(sys.argv[3]) if len(sys.argv) > 3 else 5
        
        monitor = DaiSpanMonitor(device_ip)
        monitor.run_continuous_test(duration_hours, interval_minutes)

if __name__ == "__main__":
    main()