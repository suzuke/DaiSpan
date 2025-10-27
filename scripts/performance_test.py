#!/usr/bin/env python3
"""
DaiSpan 記憶體優化性能測試腳本
測試新添加的性能監控和測試API端點
"""

import requests
import json
import time
import statistics
import argparse
from datetime import datetime
from typing import Dict, List, Optional
try:
    import matplotlib.pyplot as plt  # type: ignore
    import pandas as pd  # type: ignore
    HAS_CHARTS = True
except Exception:
    HAS_CHARTS = False

class DaiSpanPerformanceTester:
    def __init__(self, device_ip: str, port: int = 8080):
        self.base_url = f"http://{device_ip}:{port}"
        self.session = requests.Session()
        self.session.timeout = 30
        self.results = {}
        self.minimal_build = False
    
    def _get(self, path: str, **kwargs) -> Optional[requests.Response]:
        """包裝 GET 請求，統一錯誤處理與回退"""
        url = f"{self.base_url}{path}"
        timeout = kwargs.pop("timeout", 30)
        try:
            response = self.session.get(url, timeout=timeout, **kwargs)
            if response.status_code == 404:
                print(f"API {path} 未啟用 (404)")
            return response
        except requests.RequestException as exc:
            print(f"請求 {path} 失敗: {exc}")
            return None
        
    def test_connection(self) -> bool:
        """測試設備連接"""
        response = self._get("/api/health")
        if response and response.status_code == 200:
            dash_resp = self._get("/api/monitor/dashboard")
            if not dash_resp or dash_resp.status_code == 404:
                self.minimal_build = True
            else:
                self.minimal_build = False
            return True
        
        legacy = self._get("/api/monitor/dashboard")
        if legacy and legacy.status_code == 200:
            self.minimal_build = False
            return True
        return False
    
    def get_memory_stats(self) -> Optional[Dict]:
        """獲取記憶體統計信息"""
        response = self._get("/api/memory/detailed")
        if response is None:
            return None
        if response.status_code == 404:
            print("記憶體詳細 API 未啟用，改用 /api/memory/stats")
            fallback = self._get("/api/memory/stats")
            if fallback and fallback.status_code == 200:
                data = fallback.json()
                return {
                    "heap": data.get("heap", {}),
                    "memoryPressure": data.get("memoryPressure", {}),
                    "source": "stats"
                }
            return None
        if response.status_code == 200:
            return response.json()
        return None
    
    def run_performance_test(self) -> Optional[Dict]:
        """運行基本性能測試"""
        response = self._get("/api/performance/test")
        if response is None:
            return None
        if response.status_code == 404:
            print("性能測試 API 未啟用，跳過此測試")
            return {"skipped": True}
        if response.status_code == 200:
            return response.json()
        return None
    
    def run_load_test(self, iterations: int = 20, delay: int = 100) -> Optional[Dict]:
        """運行負載測試"""
        params = {'iterations': iterations, 'delay': delay}
        response = self._get("/api/performance/load", params=params)
        if response is None:
            return None
        if response.status_code == 404:
            print("負載測試 API 未啟用，跳過此測試")
            return {"skipped": True}
        if response.status_code == 200:
            return response.json()
        return None
    
    def run_benchmark_test(self) -> Optional[Dict]:
        """運行基準測試"""
        response = self._get("/api/performance/benchmark")
        if response is None:
            return None
        if response.status_code == 404:
            print("基準測試 API 未啟用，跳過此測試")
            return {"skipped": True}
        if response.status_code == 200:
            return response.json()
        return None
    
    def monitor_dashboard(self, duration: int = 60) -> List[Dict]:
        """監控儀表板數據"""
        results = []
        start_time = time.time()
        
        print(f"開始監控 {duration} 秒...")
        
        while time.time() - start_time < duration:
            response = self._get("/api/monitor/dashboard")
            if response is None:
                break
            if response.status_code == 404:
                print("儀表板 API 未啟用，結束監控")
                break
            if response.status_code == 200:
                data = response.json()
                data['collection_time'] = time.time()
                results.append(data)
                print(f"記憶體: {data['system']['freeHeap']:,} bytes, "
                      f"運行時間: {data['system']['uptime']} 秒")
            else:
                print(f"監控請求失敗: {response.status_code}")
            
            time.sleep(5)  # 每5秒採樣一次
        
        return results
    
    def stress_test(self, concurrent_requests: int = 5, duration: int = 30) -> Dict:
        """壓力測試"""
        import threading
        import queue
        
        result_queue = queue.Queue()
        start_time = time.time()
        
        def worker():
            local_results = []
            while time.time() - start_time < duration:
                request_start = time.time()
                response = self._get("/api/memory/stats")
                if response is None:
                    local_results.append({
                        'success': False,
                        'error': 'request failed',
                        'timestamp': time.time()
                    })
                    break
                if response.status_code == 404:
                    local_results.append({
                        'success': False,
                        'error': 'api disabled',
                        'timestamp': time.time()
                    })
                    self.minimal_build = True
                    break
                
                request_time = time.time() - request_start
                local_results.append({
                    'success': response.status_code == 200,
                    'response_time': request_time,
                    'timestamp': time.time()
                })
                
                time.sleep(0.1)  # 100ms間隔
            
            result_queue.put(local_results)
        
        print(f"開始壓力測試: {concurrent_requests} 個並發請求, 持續 {duration} 秒")
        
        threads = []
        for _ in range(concurrent_requests):
            thread = threading.Thread(target=worker)
            thread.start()
            threads.append(thread)
        
        for thread in threads:
            thread.join()
        
        # 收集結果
        all_results = []
        while not result_queue.empty():
            all_results.extend(result_queue.get())
        
        # 分析結果
        successful_requests = [r for r in all_results if r.get('success', False)]
        failed_requests = [r for r in all_results if not r.get('success', False)]
        
        if successful_requests:
            response_times = [r['response_time'] for r in successful_requests]
            
            return {
                'total_requests': len(all_results),
                'successful_requests': len(successful_requests),
                'failed_requests': len(failed_requests),
                'success_rate': len(successful_requests) / len(all_results) * 100,
                'avg_response_time': statistics.mean(response_times),
                'min_response_time': min(response_times),
                'max_response_time': max(response_times),
                'median_response_time': statistics.median(response_times),
                'requests_per_second': len(successful_requests) / duration
            }
        else:
            error = '所有請求都失敗了'
            if any(r.get('error') == 'api disabled' for r in all_results):
                error = 'api disabled'
            return {
                'total_requests': len(all_results),
                'successful_requests': 0,
                'failed_requests': len(failed_requests),
                'success_rate': 0,
                'error': error
            }
    
    def comprehensive_test(self) -> Dict:
        """綜合測試"""
        print("=" * 60)
        print("DaiSpan 記憶體優化性能綜合測試")
        print("=" * 60)
        
        results = {
            'timestamp': datetime.now().isoformat(),
            'device_url': self.base_url,
            'minimal_build': self.minimal_build
        }
        
        # 1. 連接測試
        print("\n1. 測試設備連接...")
        if not self.test_connection():
            print("❌ 設備連接失敗!")
            results['connection'] = False
            return results
        print("✅ 設備連接成功")
        if self.minimal_build:
            print("ℹ️ 偵測到極簡生產版，部分性能 API 可能未啟用")
        results['connection'] = True
        
        # 2. 記憶體統計
        print("\n2. 獲取記憶體統計...")
        memory_stats = self.get_memory_stats()
        if memory_stats:
            heap = memory_stats.get('heap', {})
            pressure = memory_stats.get('memoryPressure', {})
            free_heap = heap.get('free')
            fragmentation = heap.get('fragmentation')
            if free_heap is not None:
                print(f"✅ 可用記憶體: {free_heap:,} bytes")
            if fragmentation is not None:
                print(f"✅ 記憶體碎片化: {fragmentation}%")
            if isinstance(pressure, dict):
                print(f"✅ 記憶體壓力: {pressure.get('name', '未知')}")
            results['memory_stats'] = memory_stats
        else:
            print("⚠️ 無法取得詳細記憶體統計 (API 未啟用或離線)")
            results['memory_stats'] = None
        
        # 3. 基本性能測試
        print("\n3. 執行基本性能測試...")
        perf_test = self.run_performance_test()
        if perf_test and not perf_test.get('skipped'):
            print(f"✅ 記憶體分配測試: {perf_test['allocationTest']['duration']} ms")
            print(f"✅ 流式響應測試: {perf_test['streamingTest']['duration']} ms")
            print(f"✅ JSON生成測試: {perf_test['jsonTest']['duration']} ms")
            results['performance_test'] = perf_test
        elif perf_test and perf_test.get('skipped'):
            print("ℹ️ 性能測試 API 未啟用，已跳過")
            results['performance_test'] = perf_test
        else:
            print("❌ 性能測試失敗")
        
        # 4. 負載測試
        print("\n4. 執行負載測試...")
        load_test = self.run_load_test(iterations=10, delay=50)
        if load_test and not load_test.get('skipped'):
            summary = load_test['summary']
            print(f"✅ 總時間: {summary['totalTime']} ms")
            print(f"✅ 平均時間: {summary['avgTime']} ms")
            print(f"✅ 記憶體變化: {summary['heapVariation']:,} bytes")
            results['load_test'] = load_test
        elif load_test and load_test.get('skipped'):
            print("ℹ️ 負載測試 API 未啟用，已跳過")
            results['load_test'] = load_test
        else:
            print("❌ 負載測試失敗")
        
        # 5. 基準測試
        print("\n5. 執行基準測試...")
        benchmark = self.run_benchmark_test()
        if benchmark and not benchmark.get('skipped'):
            improvement = benchmark['improvement']
            print(f"✅ 時間改善: {improvement['timePercent']:.2f}%")
            print(f"✅ 記憶體改善: {improvement['memoryPercent']:.2f}%")
            results['benchmark'] = benchmark
        elif benchmark and benchmark.get('skipped'):
            print("ℹ️ 基準測試 API 未啟用，已跳過")
            results['benchmark'] = benchmark
        else:
            print("❌ 基準測試失敗")
        
        # 6. 短期監控
        print("\n6. 執行短期監控 (30秒)...")
        monitoring_data = self.monitor_dashboard(duration=30)
        if monitoring_data:
            heap_values = [d['system']['freeHeap'] for d in monitoring_data]
            print(f"✅ 記憶體監控完成，採樣 {len(monitoring_data)} 次")
            print(f"✅ 記憶體範圍: {min(heap_values):,} - {max(heap_values):,} bytes")
            results['monitoring'] = {
                'samples': len(monitoring_data),
                'min_heap': min(heap_values),
                'max_heap': max(heap_values),
                'avg_heap': sum(heap_values) // len(heap_values),
                'heap_stability': max(heap_values) - min(heap_values)
            }
        elif self.minimal_build:
            print("ℹ️ 儀表板 API 未啟用，監控測試略過")
            results['monitoring'] = {'skipped': True}
        else:
            print("❌ 監控測試失敗")
        
        # 7. 壓力測試
        print("\n7. 執行壓力測試 (20秒)...")
        stress_results = self.stress_test(concurrent_requests=3, duration=20)
        if stress_results.get('success_rate', 0) > 0:
            print(f"✅ 成功率: {stress_results['success_rate']:.2f}%")
            print(f"✅ 平均響應時間: {stress_results['avg_response_time']:.3f} 秒")
            print(f"✅ 每秒請求數: {stress_results['requests_per_second']:.2f}")
            results['stress_test'] = stress_results
        elif stress_results.get('error') == 'api disabled':
            print("ℹ️ 壓力測試 API 未啟用，已跳過")
            results['stress_test'] = {'skipped': True}
        else:
            print("❌ 壓力測試失敗")
            results['stress_test'] = stress_results
        
        return results
    
    def generate_report(self, results: Dict, output_file: str = None):
        """生成測試報告"""
        report = []
        report.append("DaiSpan 記憶體優化性能測試報告")
        report.append("=" * 50)
        report.append(f"測試時間: {results['timestamp']}")
        report.append(f"設備地址: {results['device_url']}")
        report.append("")
        
        # 連接狀態
        report.append("連接狀態:")
        report.append(f"  設備連線: {'✅ 成功' if results.get('connection') else '❌ 失敗'}")
        if results.get('minimal_build'):
            report.append("  備註: 偵測到極簡生產版 (性能 API 未啟用)")
        report.append("")
        
        # 記憶體狀態
        memory = results.get('memory_stats')
        if memory:
            heap = memory.get('heap', {})
            pressure = memory.get('memoryPressure', {})
            report.append("記憶體狀態:")
            if 'free' in heap:
                report.append(f"  可用記憶體: {heap['free']:,} bytes")
            if 'maxAlloc' in heap:
                report.append(f"  最大分配: {heap['maxAlloc']:,} bytes")
            if 'fragmentation' in heap:
                report.append(f"  碎片化率: {heap['fragmentation']}%")
            if isinstance(pressure, dict):
                report.append(f"  記憶體壓力: {pressure.get('name', '未知')}")
            if memory.get('source') == 'stats':
                report.append("  (詳細記憶體 API 未啟用，使用 /api/memory/stats)")
            report.append("")
        else:
            report.append("記憶體狀態: ⚠️ 無法取得 (API 未啟用)")
            report.append("")
        
        # 性能測試結果
        perf = results.get('performance_test')
        if perf and not perf.get('skipped'):
            report.append("基本性能測試:")
            report.append(f"  記憶體分配: {perf['allocationTest']['duration']} ms")
            report.append(f"  流式響應: {perf['streamingTest']['duration']} ms")
            report.append(f"  JSON生成: {perf['jsonTest']['duration']} ms")
            report.append(f"  總體耗時: {perf['overall']['totalDuration']} ms")
            report.append("")
        elif perf and perf.get('skipped'):
            report.append("基本性能測試: ℹ️ API 未啟用 (跳過)")
            report.append("")
        
        # 基準測試結果
        benchmark = results.get('benchmark')
        if benchmark and not benchmark.get('skipped'):
            report.append("基準測試對比:")
            report.append(f"  傳統方法耗時: {benchmark['traditional']['duration']} ms")
            report.append(f"  優化方法耗時: {benchmark['optimized']['duration']} ms")
            report.append(f"  時間改善: {benchmark['improvement']['timePercent']:.2f}%")
            report.append(f"  記憶體改善: {benchmark['improvement']['memoryPercent']:.2f}%")
            report.append("")
        elif benchmark and benchmark.get('skipped'):
            report.append("基準測試對比: ℹ️ API 未啟用 (跳過)")
            report.append("")
        
        # 負載測試結果
        load_test = results.get('load_test')
        if load_test and not load_test.get('skipped'):
            summary = load_test['summary']
            report.append("負載測試結果:")
            report.append(f"  迭代次數: {load_test['iterations']}")
            report.append(f"  總耗時: {summary['totalTime']} ms")
            report.append(f"  平均耗時: {summary['avgTime']} ms")
            report.append(f"  記憶體變化: {summary['heapVariation']:,} bytes")
            report.append("")
        elif load_test and load_test.get('skipped'):
            report.append("負載測試結果: ℹ️ API 未啟用 (跳過)")
            report.append("")
        
        # 壓力測試結果
        stress = results.get('stress_test')
        if stress and not stress.get('skipped'):
            report.append("壓力測試結果:")
            report.append(f"  總請求數: {stress['total_requests']}")
            report.append(f"  成功率: {stress.get('success_rate', 0):.2f}%")
            if 'avg_response_time' in stress:
                report.append(f"  平均響應時間: {stress['avg_response_time']:.3f} 秒")
                report.append(f"  最小響應時間: {stress['min_response_time']:.3f} 秒")
                report.append(f"  最大響應時間: {stress['max_response_time']:.3f} 秒")
                report.append(f"  每秒請求數: {stress.get('requests_per_second', 0):.2f}")
            report.append("")
        elif stress and stress.get('skipped'):
            report.append("壓力測試結果: ℹ️ API 未啟用 (跳過)")
            report.append("")
        
        # 監控結果
        monitor = results.get('monitoring')
        if monitor and not monitor.get('skipped'):
            report.append("記憶體監控結果:")
            report.append(f"  採樣次數: {monitor['samples']}")
            report.append(f"  記憶體範圍: {monitor['min_heap']:,} - {monitor['max_heap']:,} bytes")
            report.append(f"  平均記憶體: {monitor['avg_heap']:,} bytes")
            report.append(f"  記憶體穩定性: {monitor['heap_stability']:,} bytes 變化")
            report.append("")
        elif monitor and monitor.get('skipped'):
            report.append("記憶體監控結果: ℹ️ 儀表板 API 未啟用 (跳過)")
            report.append("")
        
        # 評估結論
        report.append("評估結論:")
        
        # 記憶體效率評估
        if results.get('memory_stats'):
            heap = results['memory_stats'].get('heap', {})
            fragmentation = heap.get('fragmentation', 0)
            if fragmentation < 10:
                report.append("  ✅ 記憶體碎片化良好 (< 10%)")
            elif fragmentation < 20:
                report.append("  ⚠️ 記憶體碎片化中等 (10-20%)")
            else:
                report.append("  ❌ 記憶體碎片化嚴重 (> 20%)")
        
        # 性能改善評估
        benchmark = results.get('benchmark')
        if benchmark and not benchmark.get('skipped'):
            time_improvement = benchmark['improvement']['timePercent']
            if time_improvement > 50:
                report.append("  ✅ 時間性能顯著改善 (> 50%)")
            elif time_improvement > 20:
                report.append("  ✅ 時間性能適度改善 (20-50%)")
            else:
                report.append("  ⚠️ 時間性能改善有限 (< 20%)")
        
        # 系統穩定性評估
        stress = results.get('stress_test')
        if stress and not stress.get('skipped'):
            success_rate = stress.get('success_rate', 0)
            if success_rate > 95:
                report.append("  ✅ 系統穩定性優秀 (> 95%)")
            elif success_rate > 90:
                report.append("  ✅ 系統穩定性良好 (90-95%)")
            else:
                report.append("  ❌ 系統穩定性需要改善 (< 90%)")
        
        report_text = "\n".join(report)
        
        if output_file:
            with open(output_file, 'w', encoding='utf-8') as f:
                f.write(report_text)
            print(f"\n📄 測試報告已保存到: {output_file}")
        
        print("\n" + report_text)
        
        return report_text

def main():
    parser = argparse.ArgumentParser(description='DaiSpan 記憶體優化性能測試')
    parser.add_argument('device_ip', help='設備IP地址')
    parser.add_argument('--port', type=int, default=8080, help='設備端口 (默認: 8080)')
    parser.add_argument('--output', '-o', help='輸出報告文件路徑')
    parser.add_argument('--test', choices=['all', 'memory', 'performance', 'load', 'benchmark', 'stress', 'monitor'], 
                       default='all', help='選擇測試類型')
    
    args = parser.parse_args()
    
    tester = DaiSpanPerformanceTester(args.device_ip, args.port)
    
    if args.test == 'all':
        results = tester.comprehensive_test()
        tester.generate_report(results, args.output)
    else:
        print(f"執行單項測試: {args.test}")
        # 可以在這裡添加單項測試的邏輯
        if args.test == 'memory':
            result = tester.get_memory_stats()
            print(json.dumps(result, indent=2, ensure_ascii=False))
        elif args.test == 'performance':
            result = tester.run_performance_test()
            print(json.dumps(result, indent=2, ensure_ascii=False))
        # ... 其他單項測試

if __name__ == "__main__":
    main()
