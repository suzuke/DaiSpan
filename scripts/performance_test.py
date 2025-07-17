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
import matplotlib.pyplot as plt
import pandas as pd

class DaiSpanPerformanceTester:
    def __init__(self, device_ip: str, port: int = 8080):
        self.base_url = f"http://{device_ip}:{port}"
        self.session = requests.Session()
        self.session.timeout = 30
        self.results = {}
        
    def test_connection(self) -> bool:
        """測試設備連接"""
        try:
            response = self.session.get(f"{self.base_url}/api/monitor/dashboard")
            return response.status_code == 200
        except requests.RequestException:
            return False
    
    def get_memory_stats(self) -> Optional[Dict]:
        """獲取記憶體統計信息"""
        try:
            response = self.session.get(f"{self.base_url}/api/memory/detailed")
            if response.status_code == 200:
                return response.json()
        except Exception as e:
            print(f"獲取記憶體統計失敗: {e}")
        return None
    
    def run_performance_test(self) -> Optional[Dict]:
        """運行基本性能測試"""
        try:
            response = self.session.get(f"{self.base_url}/api/performance/test")
            if response.status_code == 200:
                return response.json()
        except Exception as e:
            print(f"性能測試失敗: {e}")
        return None
    
    def run_load_test(self, iterations: int = 20, delay: int = 100) -> Optional[Dict]:
        """運行負載測試"""
        try:
            params = {'iterations': iterations, 'delay': delay}
            response = self.session.get(f"{self.base_url}/api/performance/load", params=params)
            if response.status_code == 200:
                return response.json()
        except Exception as e:
            print(f"負載測試失敗: {e}")
        return None
    
    def run_benchmark_test(self) -> Optional[Dict]:
        """運行基準測試"""
        try:
            response = self.session.get(f"{self.base_url}/api/performance/benchmark")
            if response.status_code == 200:
                return response.json()
        except Exception as e:
            print(f"基準測試失敗: {e}")
        return None
    
    def monitor_dashboard(self, duration: int = 60) -> List[Dict]:
        """監控儀表板數據"""
        results = []
        start_time = time.time()
        
        print(f"開始監控 {duration} 秒...")
        
        while time.time() - start_time < duration:
            try:
                response = self.session.get(f"{self.base_url}/api/monitor/dashboard")
                if response.status_code == 200:
                    data = response.json()
                    data['collection_time'] = time.time()
                    results.append(data)
                    print(f"記憶體: {data['system']['freeHeap']:,} bytes, "
                          f"運行時間: {data['system']['uptime']} 秒")
                else:
                    print(f"監控請求失敗: {response.status_code}")
            except Exception as e:
                print(f"監控錯誤: {e}")
            
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
                try:
                    request_start = time.time()
                    response = self.session.get(f"{self.base_url}/api/memory/stats")
                    request_time = time.time() - request_start
                    
                    local_results.append({
                        'success': response.status_code == 200,
                        'response_time': request_time,
                        'timestamp': time.time()
                    })
                except Exception as e:
                    local_results.append({
                        'success': False,
                        'error': str(e),
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
            return {
                'total_requests': len(all_results),
                'successful_requests': 0,
                'failed_requests': len(failed_requests),
                'success_rate': 0,
                'error': '所有請求都失敗了'
            }
    
    def comprehensive_test(self) -> Dict:
        """綜合測試"""
        print("=" * 60)
        print("DaiSpan 記憶體優化性能綜合測試")
        print("=" * 60)
        
        results = {
            'timestamp': datetime.now().isoformat(),
            'device_url': self.base_url
        }
        
        # 1. 連接測試
        print("\n1. 測試設備連接...")
        if not self.test_connection():
            print("❌ 設備連接失敗!")
            results['connection'] = False
            return results
        print("✅ 設備連接成功")
        results['connection'] = True
        
        # 2. 記憶體統計
        print("\n2. 獲取記憶體統計...")
        memory_stats = self.get_memory_stats()
        if memory_stats:
            print(f"✅ 可用記憶體: {memory_stats['heap']['free']:,} bytes")
            print(f"✅ 記憶體碎片化: {memory_stats['heap']['fragmentation']}%")
            print(f"✅ 記憶體壓力: {memory_stats['memoryPressure']['name']}")
            results['memory_stats'] = memory_stats
        else:
            print("❌ 記憶體統計獲取失敗")
        
        # 3. 基本性能測試
        print("\n3. 執行基本性能測試...")
        perf_test = self.run_performance_test()
        if perf_test:
            print(f"✅ 記憶體分配測試: {perf_test['allocationTest']['duration']} ms")
            print(f"✅ 流式響應測試: {perf_test['streamingTest']['duration']} ms")
            print(f"✅ JSON生成測試: {perf_test['jsonTest']['duration']} ms")
            results['performance_test'] = perf_test
        else:
            print("❌ 性能測試失敗")
        
        # 4. 負載測試
        print("\n4. 執行負載測試...")
        load_test = self.run_load_test(iterations=10, delay=50)
        if load_test:
            summary = load_test['summary']
            print(f"✅ 總時間: {summary['totalTime']} ms")
            print(f"✅ 平均時間: {summary['avgTime']} ms")
            print(f"✅ 記憶體變化: {summary['heapVariation']:,} bytes")
            results['load_test'] = load_test
        else:
            print("❌ 負載測試失敗")
        
        # 5. 基準測試
        print("\n5. 執行基準測試...")
        benchmark = self.run_benchmark_test()
        if benchmark:
            improvement = benchmark['improvement']
            print(f"✅ 時間改善: {improvement['timePercent']:.2f}%")
            print(f"✅ 記憶體改善: {improvement['memoryPercent']:.2f}%")
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
        report.append("")
        
        # 記憶體狀態
        if 'memory_stats' in results:
            memory = results['memory_stats']
            report.append("記憶體狀態:")
            report.append(f"  可用記憶體: {memory['heap']['free']:,} bytes")
            report.append(f"  最大分配: {memory['heap']['maxAlloc']:,} bytes")
            report.append(f"  碎片化率: {memory['heap']['fragmentation']}%")
            report.append(f"  記憶體壓力: {memory['memoryPressure']['name']}")
            report.append("")
        
        # 性能測試結果
        if 'performance_test' in results:
            perf = results['performance_test']
            report.append("基本性能測試:")
            report.append(f"  記憶體分配: {perf['allocationTest']['duration']} ms")
            report.append(f"  流式響應: {perf['streamingTest']['duration']} ms")
            report.append(f"  JSON生成: {perf['jsonTest']['duration']} ms")
            report.append(f"  總體耗時: {perf['overall']['totalDuration']} ms")
            report.append("")
        
        # 基準測試結果
        if 'benchmark' in results:
            benchmark = results['benchmark']
            report.append("基準測試對比:")
            report.append(f"  傳統方法耗時: {benchmark['traditional']['duration']} ms")
            report.append(f"  優化方法耗時: {benchmark['optimized']['duration']} ms")
            report.append(f"  時間改善: {benchmark['improvement']['timePercent']:.2f}%")
            report.append(f"  記憶體改善: {benchmark['improvement']['memoryPercent']:.2f}%")
            report.append("")
        
        # 負載測試結果
        if 'load_test' in results:
            load = results['load_test']['summary']
            report.append("負載測試結果:")
            report.append(f"  迭代次數: {results['load_test']['iterations']}")
            report.append(f"  總耗時: {load['totalTime']} ms")
            report.append(f"  平均耗時: {load['avgTime']} ms")
            report.append(f"  記憶體變化: {load['heapVariation']:,} bytes")
            report.append("")
        
        # 壓力測試結果
        if 'stress_test' in results:
            stress = results['stress_test']
            report.append("壓力測試結果:")
            report.append(f"  總請求數: {stress['total_requests']}")
            report.append(f"  成功率: {stress['success_rate']:.2f}%")
            if 'avg_response_time' in stress:
                report.append(f"  平均響應時間: {stress['avg_response_time']:.3f} 秒")
                report.append(f"  最小響應時間: {stress['min_response_time']:.3f} 秒")
                report.append(f"  最大響應時間: {stress['max_response_time']:.3f} 秒")
                report.append(f"  每秒請求數: {stress['requests_per_second']:.2f}")
            report.append("")
        
        # 監控結果
        if 'monitoring' in results:
            monitor = results['monitoring']
            report.append("記憶體監控結果:")
            report.append(f"  採樣次數: {monitor['samples']}")
            report.append(f"  記憶體範圍: {monitor['min_heap']:,} - {monitor['max_heap']:,} bytes")
            report.append(f"  平均記憶體: {monitor['avg_heap']:,} bytes")
            report.append(f"  記憶體穩定性: {monitor['heap_stability']:,} bytes 變化")
            report.append("")
        
        # 評估結論
        report.append("評估結論:")
        
        # 記憶體效率評估
        if 'memory_stats' in results:
            fragmentation = results['memory_stats']['heap']['fragmentation']
            if fragmentation < 10:
                report.append("  ✅ 記憶體碎片化良好 (< 10%)")
            elif fragmentation < 20:
                report.append("  ⚠️ 記憶體碎片化中等 (10-20%)")
            else:
                report.append("  ❌ 記憶體碎片化嚴重 (> 20%)")
        
        # 性能改善評估
        if 'benchmark' in results:
            time_improvement = results['benchmark']['improvement']['timePercent']
            if time_improvement > 50:
                report.append("  ✅ 時間性能顯著改善 (> 50%)")
            elif time_improvement > 20:
                report.append("  ✅ 時間性能適度改善 (20-50%)")
            else:
                report.append("  ⚠️ 時間性能改善有限 (< 20%)")
        
        # 系統穩定性評估
        if 'stress_test' in results:
            success_rate = results['stress_test']['success_rate']
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