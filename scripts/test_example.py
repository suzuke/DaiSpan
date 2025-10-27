#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DaiSpan 測試範例
直接指定設備IP進行測試
"""

import requests
import time
import sys
from datetime import datetime

def test_device(device_ip, port=80):
    """測試指定IP的DaiSpan設備"""
    base_url = f"http://{device_ip}:{port}"
    
    print(f"測試設備: {base_url}")
    print(f"時間: {datetime.now()}")
    print("-" * 40)
    
    # 測試頁面
    pages = [
        ("/", "主頁"),
        ("/wifi", "WiFi設定"),
        ("/homekit", "HomeKit設定")
    ]
    
    success_count = 0
    
    for path, name in pages:
        url = base_url + path
        print(f"測試 {name}...", end=" ")
        
        try:
            start = time.time()
            response = requests.get(url, timeout=10)
            duration = time.time() - start
            
            if response.status_code == 200:
                print(f"✓ 成功 ({duration:.2f}s)")
                success_count += 1
            else:
                print(f"✗ HTTP {response.status_code}")
                
        except Exception as e:
            print(f"✗ 錯誤: {e}")
    
    print("-" * 40)
    print(f"測試結果: {success_count}/{len(pages)} 成功")
    
    return success_count == len(pages)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("使用方法: python3 test_example.py <設備IP>")
        print("範例: python3 test_example.py 192.168.4.1")
        sys.exit(1)
    
    device_ip = sys.argv[1]
    success = test_device(device_ip)
    
    if success:
        print("🟢 設備運行正常")
    else:
        print("🔴 設備有問題")
