#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DaiSpan 異步WiFi掃描器測試
測試AsyncWiFiScanner組件的功能和性能
"""

import requests
import time
import json
import threading
import statistics
from typing import List, Dict, Optional
from dataclasses import dataclass
from datetime import datetime


@dataclass
class ScanResult:
    """掃描結果數據類"""
    success: bool
    response_time: float
    networks_found: int
    error_message: Optional[str] = None
    timestamp: datetime = None
    
    def __post_init__(self):
        if self.timestamp is None:
            self.timestamp = datetime.now()


class AsyncWiFiScannerTester:
    """異步WiFi掃描器測試類"""
    
    def __init__(self, device_ip: str = "192.168.4.1", timeout: int = 30):
        self.device_ip = device_ip
        self.base_url = f"http://{device_ip}"
        self.timeout = timeout
        self.session = requests.Session()
        self.session.timeout = timeout
        
        # 測試結果
        self.test_results: List[ScanResult] = []
        self.concurrent_results: Dict[str, List[ScanResult]] = {}
        
    def test_device_connectivity(self) -> bool:
        """測試設備連通性"""
        try:
            response = self.session.get(f"{self.base_url}/", timeout=5)
            if response.status_code == 200:
                print(f"✅ 設備連通性正常: {self.device_ip}")
                return True
            else:
                print(f"❌ 設備響應異常: HTTP {response.status_code}")
                return False
        except Exception as e:
            print(f"❌ 設備連接失敗: {e}")
            return False
    
    def test_sync_scan(self) -> ScanResult:
        """測試同步掃描（原始阻塞方式）"""
        print("\n🔍 測試同步WiFi掃描...")
        
        start_time = time.time()
        try:
            response = self.session.get(f"{self.base_url}/scan")
            end_time = time.time()
            
            if response.status_code == 200:
                networks = response.json()
                result = ScanResult(
                    success=True,
                    response_time=end_time - start_time,
                    networks_found=len(networks)
                )
                print(f"✅ 同步掃描成功: {len(networks)} 個網路, 耗時 {result.response_time:.2f}秒")
                return result
            else:
                result = ScanResult(
                    success=False,
                    response_time=end_time - start_time,
                    networks_found=0,
                    error_message=f"HTTP {response.status_code}"
                )
                print(f"❌ 同步掃描失敗: {result.error_message}")
                return result
                
        except Exception as e:
            end_time = time.time()
            result = ScanResult(
                success=False,
                response_time=end_time - start_time,
                networks_found=0,
                error_message=str(e)
            )
            print(f"❌ 同步掃描異常: {result.error_message}")
            return result
    
    def test_async_scan(self) -> ScanResult:
        """測試異步掃描"""
        print("\n🚀 測試異步WiFi掃描...")
        
        start_time = time.time()
        try:
            response = self.session.post(f"{self.base_url}/scan-async")
            end_time = time.time()
            
            if response.status_code == 202:
                data = response.json()
                request_id = data.get('requestId')
                
                print(f"✅ 異步掃描請求已接受: 請求ID {request_id}, 響應時間 {end_time - start_time:.3f}秒")
                
                # 等待掃描完成並獲取結果
                scan_completed = False
                max_wait_time = 15  # 最大等待15秒
                wait_start = time.time()
                
                while not scan_completed and (time.time() - wait_start) < max_wait_time:
                    time.sleep(0.5)
                    
                    # 檢查緩存結果
                    try:
                        cache_response = self.session.get(f"{self.base_url}/scan")
                        if cache_response.status_code == 200:
                            networks = cache_response.json()
                            if len(networks) > 0:
                                scan_completed = True
                                total_time = time.time() - start_time
                                
                                result = ScanResult(
                                    success=True,
                                    response_time=total_time,
                                    networks_found=len(networks)
                                )
                                
                                print(f"✅ 異步掃描完成: {len(networks)} 個網路, 總耗時 {total_time:.2f}秒")
                                return result
                    except:
                        pass
                
                if not scan_completed:
                    result = ScanResult(
                        success=False,
                        response_time=time.time() - start_time,
                        networks_found=0,
                        error_message="掃描超時"
                    )
                    print(f"❌ 異步掃描超時")
                    return result
                    
            else:
                result = ScanResult(
                    success=False,
                    response_time=end_time - start_time,
                    networks_found=0,
                    error_message=f"HTTP {response.status_code}"
                )
                print(f"❌ 異步掃描失敗: {result.error_message}")
                return result
                
        except Exception as e:
            end_time = time.time()
            result = ScanResult(
                success=False,
                response_time=end_time - start_time,
                networks_found=0,
                error_message=str(e)
            )
            print(f"❌ 異步掃描異常: {result.error_message}")
            return result
    
    def test_concurrent_async_scans(self, num_threads: int = 5) -> Dict[str, List[ScanResult]]:
        """測試並發異步掃描"""
        print(f"\n⚡ 測試並發異步掃描 ({num_threads} 個並發請求)...")
        
        results = {'success': [], 'failed': []}
        threads = []
        
        def scan_worker(thread_id: int):
            """工作線程"""
            try:
                start_time = time.time()
                response = self.session.post(f"{self.base_url}/scan-async")
                end_time = time.time()
                
                if response.status_code == 202:
                    result = ScanResult(
                        success=True,
                        response_time=end_time - start_time,
                        networks_found=0  # 並發測試主要測試請求接受能力
                    )
                    results['success'].append(result)
                    print(f"  ✅ 線程 {thread_id}: 請求成功 ({result.response_time:.3f}秒)")
                else:
                    result = ScanResult(
                        success=False,
                        response_time=end_time - start_time,
                        networks_found=0,
                        error_message=f"HTTP {response.status_code}"
                    )
                    results['failed'].append(result)
                    print(f"  ❌ 線程 {thread_id}: 請求失敗 ({result.error_message})")
                    
            except Exception as e:
                result = ScanResult(
                    success=False,
                    response_time=0,
                    networks_found=0,
                    error_message=str(e)
                )
                results['failed'].append(result)
                print(f"  ❌ 線程 {thread_id}: 異常 ({str(e)})")
        
        # 啟動並發線程
        for i in range(num_threads):
            thread = threading.Thread(target=scan_worker, args=(i,))
            threads.append(thread)
            thread.start()
        
        # 等待所有線程完成
        for thread in threads:
            thread.join()
        
        success_count = len(results['success'])
        failed_count = len(results['failed'])
        
        print(f"  並發測試完成: {success_count} 成功, {failed_count} 失敗")
        
        if success_count > 0:
            avg_response_time = statistics.mean([r.response_time for r in results['success']])
            print(f"  平均響應時間: {avg_response_time:.3f}秒")
        
        return results
    
    def test_performance_comparison(self, iterations: int = 3) -> None:
        """性能對比測試"""
        print(f"\n📊 性能對比測試 ({iterations} 次迭代)...")
        
        sync_results = []
        async_results = []
        
        for i in range(iterations):
            print(f"\n--- 第 {i+1} 次迭代 ---")
            
            # 測試同步掃描
            sync_result = self.test_sync_scan()
            sync_results.append(sync_result)
            
            # 等待一段時間
            time.sleep(2)
            
            # 測試異步掃描
            async_result = self.test_async_scan()
            async_results.append(async_result)
            
            # 等待一段時間
            time.sleep(2)
        
        # 計算統計數據
        sync_times = [r.response_time for r in sync_results if r.success]
        async_times = [r.response_time for r in async_results if r.success]
        
        print(f"\n📈 性能統計:")
        print(f"同步掃描:")
        print(f"  成功率: {len(sync_times)}/{iterations} ({len(sync_times)/iterations*100:.1f}%)")
        if sync_times:
            print(f"  平均耗時: {statistics.mean(sync_times):.2f}秒")
            print(f"  最小耗時: {min(sync_times):.2f}秒")
            print(f"  最大耗時: {max(sync_times):.2f}秒")
        
        print(f"\n異步掃描:")
        print(f"  成功率: {len(async_times)}/{iterations} ({len(async_times)/iterations*100:.1f}%)")
        if async_times:
            print(f"  平均耗時: {statistics.mean(async_times):.2f}秒")
            print(f"  最小耗時: {min(async_times):.2f}秒")
            print(f"  最大耗時: {max(async_times):.2f}秒")
        
        # 計算改進
        if sync_times and async_times:
            improvement = (statistics.mean(sync_times) - statistics.mean(async_times)) / statistics.mean(sync_times) * 100
            print(f"\n⚡ 性能改進: {improvement:.1f}%")
    
    def test_error_handling(self) -> None:
        """測試錯誤處理"""
        print("\n🛡️ 測試錯誤處理...")
        
        # 測試快速連續請求
        print("測試快速連續請求...")
        for i in range(10):
            try:
                response = self.session.post(f"{self.base_url}/scan-async", timeout=1)
                if response.status_code == 503:
                    print(f"  第 {i+1} 次請求被拒絕 (預期行為)")
                    break
                elif response.status_code == 202:
                    print(f"  第 {i+1} 次請求被接受")
                time.sleep(0.1)
            except Exception as e:
                print(f"  第 {i+1} 次請求異常: {e}")
    
    def run_all_tests(self) -> None:
        """運行所有測試"""
        print("🚀 開始 DaiSpan 異步WiFi掃描器測試")
        print("=" * 50)
        
        # 連通性測試
        if not self.test_device_connectivity():
            print("❌ 設備連通性測試失敗，退出測試")
            return
        
        # 性能對比測試
        self.test_performance_comparison()
        
        # 並發測試
        self.test_concurrent_async_scans()
        
        # 錯誤處理測試
        self.test_error_handling()
        
        print("\n✅ 所有測試完成")
        print("=" * 50)


def main():
    """主函數"""
    import argparse
    
    parser = argparse.ArgumentParser(description='DaiSpan 異步WiFi掃描器測試')
    parser.add_argument('--ip', default='192.168.4.1', help='設備IP地址')
    parser.add_argument('--timeout', type=int, default=30, help='請求超時時間(秒)')
    parser.add_argument('--iterations', type=int, default=3, help='性能測試迭代次數')
    parser.add_argument('--concurrent', type=int, default=5, help='並發請求數量')
    
    args = parser.parse_args()
    
    tester = AsyncWiFiScannerTester(args.ip, args.timeout)
    tester.run_all_tests()


if __name__ == "__main__":
    main()