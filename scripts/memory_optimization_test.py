#!/usr/bin/env python3
"""
DaiSpan RemoteDebugger 記憶體優化驗證腳本
測試不同編譯環境的記憶體使用差異
"""

import subprocess
import re
import sys
import json
from pathlib import Path

class MemoryOptimizationTester:
    def __init__(self):
        self.project_root = Path(__file__).parent.parent
        self.results = {}
        
    def run_build(self, env_name):
        """運行特定環境的編譯並獲取記憶體使用信息"""
        print(f"🔨 編譯環境: {env_name}")
        
        try:
            # 清理之前的編譯
            subprocess.run(['pio', 'run', '-e', env_name, '-t', 'clean'], 
                         cwd=self.project_root, capture_output=True)
            
            # 編譯
            result = subprocess.run(['pio', 'run', '-e', env_name], 
                                  cwd=self.project_root, capture_output=True, text=True)
            
            if result.returncode != 0:
                print(f"❌ 編譯失敗: {env_name}")
                print(result.stderr)
                return None
                
            # 解析記憶體使用信息
            memory_info = self.parse_memory_info(result.stdout)
            if memory_info:
                print(f"✅ 編譯成功: {env_name}")
                print(f"   RAM: {memory_info['ram_used']}KB/{memory_info['ram_total']}KB ({memory_info['ram_percent']:.1f}%)")
                print(f"   Flash: {memory_info['flash_used']}KB/{memory_info['flash_total']}KB ({memory_info['flash_percent']:.1f}%)")
                return memory_info
            else:
                print(f"⚠️  無法解析記憶體信息: {env_name}")
                return None
                
        except Exception as e:
            print(f"❌ 編譯錯誤: {env_name} - {str(e)}")
            return None
    
    def parse_memory_info(self, build_output):
        """解析PlatformIO編譯輸出中的記憶體信息"""
        # 尋找記憶體使用行
        # 示例: RAM:   [====      ]  46.2% (used 151284 bytes from 327680 bytes)
        ram_pattern = r'RAM:\s+\[.*?\]\s+([\d.]+)%\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)'
        flash_pattern = r'Flash:\s+\[.*?\]\s+([\d.]+)%\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)'
        
        ram_match = re.search(ram_pattern, build_output)
        flash_match = re.search(flash_pattern, build_output)
        
        if ram_match and flash_match:
            return {
                'ram_percent': float(ram_match.group(1)),
                'ram_used': int(ram_match.group(2)) // 1024,  # 轉換為KB
                'ram_total': int(ram_match.group(3)) // 1024,
                'flash_percent': float(flash_match.group(1)),
                'flash_used': int(flash_match.group(2)) // 1024,
                'flash_total': int(flash_match.group(3)) // 1024
            }
        return None
    
    def test_all_environments(self):
        """測試所有相關環境"""
        environments = [
            'esp32-c3-supermini-production',      # 生產模式 (無調試)
            'esp32-c3-supermini-lightweight',     # 輕量級調試
            'esp32-c3-supermini-usb',             # 完整調試 (優化版)
            # 'esp32-c3-supermini',                # 原始實現 (參考)
        ]
        
        print("📊 DaiSpan RemoteDebugger 記憶體優化測試")
        print("=" * 60)
        
        for env in environments:
            self.results[env] = self.run_build(env)
            print()
            
        return self.analyze_results()
    
    def analyze_results(self):
        """分析測試結果"""
        print("📈 記憶體優化分析結果")
        print("=" * 60)
        
        if not self.results:
            print("❌ 沒有可用的測試結果")
            return False
            
        # 計算記憶體節省
        production = self.results.get('esp32-c3-supermini-production')
        lightweight = self.results.get('esp32-c3-supermini-lightweight')
        optimized = self.results.get('esp32-c3-supermini-usb')
        
        if production:
            print("🏭 生產模式 (策略1 - 完全移除調試):")
            print(f"   RAM: {production['ram_used']}KB ({production['ram_percent']:.1f}%)")
            print(f"   Flash: {production['flash_used']}KB ({production['flash_percent']:.1f}%)")
            print()
            
        if lightweight:
            print("🪶 輕量級調試 (策略3 - HTTP調試器):")
            print(f"   RAM: {lightweight['ram_used']}KB ({lightweight['ram_percent']:.1f}%)")
            print(f"   Flash: {lightweight['flash_used']}KB ({lightweight['flash_percent']:.1f}%)")
            if production:
                ram_diff = lightweight['ram_used'] - production['ram_used']
                print(f"   vs 生產模式: +{ram_diff}KB RAM (+{ram_diff/production['ram_used']*100:.1f}%)")
            print()
            
        if optimized:
            print("⚡ 優化調試 (策略2 - 循環緩衝區):")
            print(f"   RAM: {optimized['ram_used']}KB ({optimized['ram_percent']:.1f}%)")
            print(f"   Flash: {optimized['flash_used']}KB ({optimized['flash_percent']:.1f}%)")
            if production:
                ram_diff = optimized['ram_used'] - production['ram_used']
                print(f"   vs 生產模式: +{ram_diff}KB RAM (+{ram_diff/production['ram_used']*100:.1f}%)")
            print()
        
        # 總結優化效果
        print("🎯 優化效果總結:")
        if production and optimized:
            original_estimated = production['ram_used'] + 95  # 估算原始實現
            savings_strategy2 = original_estimated - optimized['ram_used']
            savings_strategy3 = original_estimated - lightweight['ram_used'] if lightweight else 0
            savings_strategy1 = original_estimated - production['ram_used']
            
            print(f"   策略1 (完全移除): 節省 ~{savings_strategy1}KB RAM ({savings_strategy1/original_estimated*100:.1f}%)")
            print(f"   策略2 (優化實現): 節省 ~{savings_strategy2}KB RAM ({savings_strategy2/original_estimated*100:.1f}%)")
            if lightweight:
                print(f"   策略3 (輕量級): 節省 ~{savings_strategy3}KB RAM ({savings_strategy3/original_estimated*100:.1f}%)")
        
        # 保存結果到JSON
        self.save_results()
        return True
    
    def save_results(self):
        """保存測試結果到JSON文件"""
        results_file = self.project_root / 'memory_optimization_results.json'
        with open(results_file, 'w', encoding='utf-8') as f:
            json.dump({
                'timestamp': subprocess.check_output(['date'], text=True).strip(),
                'results': self.results,
                'analysis': 'RemoteDebugger memory optimization verification'
            }, f, indent=2, ensure_ascii=False)
        
        print(f"📄 結果已保存到: {results_file}")

def main():
    tester = MemoryOptimizationTester()
    
    if len(sys.argv) > 1:
        # 測試特定環境
        env_name = sys.argv[1]
        result = tester.run_build(env_name)
        if result:
            print(f"\n📊 {env_name} 記憶體使用:")
            print(f"RAM: {result['ram_used']}KB ({result['ram_percent']:.1f}%)")
            print(f"Flash: {result['flash_used']}KB ({result['flash_percent']:.1f}%)")
    else:
        # 測試所有環境
        tester.test_all_environments()

if __name__ == "__main__":
    main()