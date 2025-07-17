#!/usr/bin/env python3
"""
DaiSpan 記憶體優化簡單測試腳本
只使用Python標準庫，無外部依賴
"""

import json
import time
import statistics
import argparse
from datetime import datetime
from typing import Dict, List, Optional

try:
    import urllib.request
    import urllib.parse
    import urllib.error
except ImportError:
    print("需要Python 3.x環境")
    exit(1)

class SimpleDaiSpanTester:
    def __init__(self, device_ip: str, port: int = 8080):
        self.base_url = f"http://{device_ip}:{port}"
        self.timeout = 30
        
    def make_request(self, endpoint: str, params: Dict = None) -> Optional[Dict]:
        """發送HTTP請求"""
        url = f"{self.base_url}{endpoint}"
        if params:
            url += "?" + urllib.parse.urlencode(params)
        
        try:
            with urllib.request.urlopen(url, timeout=self.timeout) as response:
                if response.status == 200:
                    content = response.read().decode('utf-8')
                    return json.loads(content)
                else:
                    print(f"HTTP錯誤: {response.status}")
                    return None
        except urllib.error.URLError as e:
            print(f"連接錯誤: {e}")
            return None
        except json.JSONDecodeError as e:
            print(f"JSON解析錯誤: {e}")
            return None
        except Exception as e:
            print(f"請求失敗: {e}")
            return None
    
    def test_connection(self) -> bool:
        """測試設備連接"""
        result = self.make_request("/api/monitor/dashboard")
        return result is not None and result.get('status') == 'success'
    
    def get_memory_stats(self) -> Optional[Dict]:
        """獲取記憶體統計"""
        return self.make_request("/api/memory/detailed")
    
    def run_performance_test(self) -> Optional[Dict]:
        """運行性能測試"""
        return self.make_request("/api/performance/test")
    
    def run_load_test(self, iterations: int = 10, delay: int = 100) -> Optional[Dict]:
        """運行負載測試"""
        params = {'iterations': iterations, 'delay': delay}
        return self.make_request("/api/performance/load", params)
    
    def run_benchmark_test(self) -> Optional[Dict]:
        """運行基準測試"""
        return self.make_request("/api/performance/benchmark")
    
    def monitor_dashboard(self, duration: int = 30, interval: int = 5) -> List[Dict]:
        """監控儀表板"""
        results = []
        start_time = time.time()
        
        print(f"開始監控 {duration} 秒，每 {interval} 秒採樣...")
        
        while time.time() - start_time < duration:
            data = self.make_request("/api/monitor/dashboard")
            if data:
                data['collection_time'] = time.time()
                results.append(data)
                
                heap = data['system']['freeHeap']
                uptime = data['system']['uptime']
                print(f"[{int(time.time() - start_time):3d}s] 記憶體: {heap:,} bytes, 運行: {uptime}s")
            else:
                print(f"[{int(time.time() - start_time):3d}s] 監控失敗")
            
            time.sleep(interval)
        
        return results
    
    def quick_test(self) -> Dict:
        """快速測試"""
        print("DaiSpan 記憶體優化快速測試")
        print("=" * 40)
        
        results = {
            'timestamp': datetime.now().isoformat(),
            'device_url': self.base_url,
            'tests': {}
        }
        
        # 1. 連接測試
        print("\\n1. 測試連接...")
        if self.test_connection():
            print("   ✅ 連接成功")
            results['tests']['connection'] = True
        else:
            print("   ❌ 連接失敗")
            results['tests']['connection'] = False
            return results
        
        # 2. 記憶體狀態
        print("\\n2. 檢查記憶體狀態...")
        memory_stats = self.get_memory_stats()
        if memory_stats:
            heap = memory_stats['heap']
            pressure = memory_stats['memoryPressure']
            
            print(f"   可用記憶體: {heap['free']:,} bytes")
            print(f"   記憶體碎片: {heap['fragmentation']}%")
            print(f"   記憶體壓力: {pressure['name']}")
            
            results['tests']['memory'] = {
                'free_heap': heap['free'],
                'fragmentation': heap['fragmentation'],
                'pressure': pressure['name']
            }
        else:
            print("   ❌ 記憶體狀態獲取失敗")
            results['tests']['memory'] = None
        
        # 3. 基本性能測試
        print("\\n3. 基本性能測試...")
        perf_test = self.run_performance_test()
        if perf_test:
            alloc = perf_test['allocationTest']
            stream = perf_test['streamingTest']
            json_test = perf_test['jsonTest']
            overall = perf_test['overall']
            
            print(f"   記憶體分配: {alloc['duration']} ms")
            print(f"   流式響應: {stream['duration']} ms")
            print(f"   JSON生成: {json_test['duration']} ms")
            print(f"   總計耗時: {overall['totalDuration']} ms")
            print(f"   記憶體變化: {overall['heapDiff']} bytes")
            
            results['tests']['performance'] = {
                'allocation_ms': alloc['duration'],
                'streaming_ms': stream['duration'],
                'json_ms': json_test['duration'],
                'total_ms': overall['totalDuration'],
                'heap_diff': overall['heapDiff']
            }
        else:
            print("   ❌ 性能測試失敗")
            results['tests']['performance'] = None
        
        # 4. 基準測試
        print("\\n4. 基準性能對比...")
        benchmark = self.run_benchmark_test()
        if benchmark:
            traditional = benchmark['traditional']
            optimized = benchmark['optimized']
            improvement = benchmark['improvement']
            
            print(f"   傳統方法: {traditional['duration']} ms")
            print(f"   優化方法: {optimized['duration']} ms")
            print(f"   時間改善: {improvement['timePercent']:.1f}%")
            print(f"   記憶體改善: {improvement['memoryPercent']:.1f}%")
            
            results['tests']['benchmark'] = {
                'traditional_ms': traditional['duration'],
                'optimized_ms': optimized['duration'],
                'time_improvement': improvement['timePercent'],
                'memory_improvement': improvement['memoryPercent']
            }
        else:
            print("   ❌ 基準測試失敗")
            results['tests']['benchmark'] = None
        
        # 5. 輕量負載測試
        print("\\n5. 輕量負載測試 (5次迭代)...")
        load_test = self.run_load_test(iterations=5, delay=200)
        if load_test:
            summary = load_test['summary']
            
            print(f"   總耗時: {summary['totalTime']} ms")
            print(f"   平均耗時: {summary['avgTime']} ms")
            print(f"   記憶體變化: {summary['heapVariation']:,} bytes")
            
            results['tests']['load'] = {
                'total_time': summary['totalTime'],
                'avg_time': summary['avgTime'],
                'heap_variation': summary['heapVariation']
            }
        else:
            print("   ❌ 負載測試失敗")
            results['tests']['load'] = None
        
        return results
    
    def detailed_test(self) -> Dict:
        """詳細測試"""
        print("DaiSpan 記憶體優化詳細測試")
        print("=" * 40)
        
        # 先執行快速測試
        results = self.quick_test()
        
        if not results['tests']['connection']:
            return results
        
        # 6. 短期監控
        print("\\n6. 短期記憶體監控 (30秒)...")
        monitoring_data = self.monitor_dashboard(duration=30, interval=3)
        
        if monitoring_data:
            heap_values = [d['system']['freeHeap'] for d in monitoring_data]
            
            min_heap = min(heap_values)
            max_heap = max(heap_values)
            avg_heap = sum(heap_values) // len(heap_values)
            stability = max_heap - min_heap
            
            print(f"   採樣次數: {len(monitoring_data)}")
            print(f"   記憶體範圍: {min_heap:,} - {max_heap:,} bytes")
            print(f"   平均記憶體: {avg_heap:,} bytes")
            print(f"   穩定性: {stability:,} bytes 變化")
            
            results['tests']['monitoring'] = {
                'samples': len(monitoring_data),
                'min_heap': min_heap,
                'max_heap': max_heap,
                'avg_heap': avg_heap,
                'stability': stability
            }
        else:
            print("   ❌ 監控失敗")
            results['tests']['monitoring'] = None
        
        # 7. 連續請求測試
        print("\\n7. 連續請求穩定性測試 (10次)...")
        response_times = []
        success_count = 0
        
        for i in range(10):
            start_time = time.time()
            data = self.make_request("/api/memory/stats")
            response_time = time.time() - start_time
            
            if data:
                success_count += 1
                response_times.append(response_time)
                print(f"   請求 {i+1}: {response_time:.3f}s ✅")
            else:
                print(f"   請求 {i+1}: 失敗 ❌")
            
            time.sleep(0.5)  # 500ms間隔
        
        if response_times:
            avg_response = statistics.mean(response_times)
            min_response = min(response_times)
            max_response = max(response_times)
            
            print(f"   成功率: {success_count}/10 ({success_count*10}%)")
            print(f"   平均響應: {avg_response:.3f}s")
            print(f"   響應範圍: {min_response:.3f}s - {max_response:.3f}s")
            
            results['tests']['stability'] = {
                'success_rate': success_count * 10,
                'avg_response': avg_response,
                'min_response': min_response,
                'max_response': max_response
            }
        else:
            print("   ❌ 所有請求都失敗")
            results['tests']['stability'] = None
        
        return results
    
    def generate_simple_report(self, results: Dict):
        """生成簡單報告"""
        print("\\n" + "=" * 50)
        print("測試報告摘要")
        print("=" * 50)
        print(f"測試時間: {results['timestamp']}")
        print(f"設備地址: {results['device_url']}")
        print()
        
        tests = results['tests']
        
        # 連接狀態
        print("📡 連接狀態:", "✅ 正常" if tests.get('connection') else "❌ 失敗")
        
        # 記憶體狀態
        if tests.get('memory'):
            memory = tests['memory']
            print(f"💾 記憶體狀態: {memory['free_heap']:,} bytes 可用")
            print(f"   碎片化: {memory['fragmentation']}%")
            print(f"   壓力等級: {memory['pressure']}")
        
        # 性能評估
        if tests.get('performance'):
            perf = tests['performance']
            print(f"⚡ 性能表現: 總耗時 {perf['total_ms']} ms")
            print(f"   記憶體變化: {perf['heap_diff']} bytes")
        
        # 優化效果
        if tests.get('benchmark'):
            bench = tests['benchmark']
            print(f"📈 優化效果: 時間改善 {bench['time_improvement']:.1f}%")
            print(f"   記憶體改善: {bench['memory_improvement']:.1f}%")
        
        # 穩定性
        if tests.get('stability'):
            stability = tests['stability']
            print(f"🔒 系統穩定: 成功率 {stability['success_rate']}%")
            print(f"   平均響應: {stability['avg_response']:.3f}s")
        
        # 記憶體監控
        if tests.get('monitoring'):
            monitor = tests['monitoring']
            print(f"📊 記憶體監控: 變化 {monitor['stability']:,} bytes")
            print(f"   平均使用: {monitor['avg_heap']:,} bytes")
        
        print()
        print("評估結論:")
        
        # 綜合評估
        score = 0
        total_checks = 0
        
        if tests.get('connection'):
            score += 1
            total_checks += 1
        
        if tests.get('memory'):
            total_checks += 1
            if tests['memory']['fragmentation'] < 15:
                score += 1
        
        if tests.get('benchmark'):
            total_checks += 1
            if tests['benchmark']['time_improvement'] > 20:
                score += 1
        
        if tests.get('stability'):
            total_checks += 1
            if tests['stability']['success_rate'] >= 90:
                score += 1
        
        if total_checks > 0:
            overall_score = (score / total_checks) * 100
            if overall_score >= 80:
                print("✅ 系統整體狀態優秀")
            elif overall_score >= 60:
                print("⚠️ 系統整體狀態良好")
            else:
                print("❌ 系統需要優化")
            
            print(f"   綜合評分: {overall_score:.0f}/100")
        
        print("=" * 50)

def main():
    parser = argparse.ArgumentParser(description='DaiSpan 簡單性能測試')
    parser.add_argument('device_ip', help='設備IP地址')
    parser.add_argument('--port', type=int, default=8080, help='設備端口')
    parser.add_argument('--test', choices=['quick', 'detailed'], default='quick', 
                       help='測試類型: quick=快速測試, detailed=詳細測試')
    parser.add_argument('--json', action='store_true', help='輸出JSON格式結果')
    
    args = parser.parse_args()
    
    tester = SimpleDaiSpanTester(args.device_ip, args.port)
    
    if args.test == 'quick':
        results = tester.quick_test()
    else:
        results = tester.detailed_test()
    
    if args.json:
        print(json.dumps(results, indent=2, ensure_ascii=False))
    else:
        tester.generate_simple_report(results)

if __name__ == "__main__":
    main()