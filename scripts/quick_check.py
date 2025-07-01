#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DaiSpan 快速狀態檢查腳本
用於快速驗證設備是否正常運行
"""

import requests
import time
import sys
from datetime import datetime

def check_daispan_status(device_ip="192.168.4.1", port=80):
    """快速檢查DaiSpan設備狀態"""
    base_url = f"http://{device_ip}:{port}"
    
    print(f"正在檢查 DaiSpan 設備: {base_url}")
    print(f"檢查時間: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 50)
    
    # 測試頁面
    pages = {
        "/": "主頁面",
        "/wifi": "WiFi設定",
        "/homekit": "HomeKit設定", 
        "/simulation-toggle": "模擬切換",
        "/ota": "OTA更新"
    }
    
    results = []
    
    for path, name in pages.items():
        url = base_url + path
        print(f"檢查 {name}...", end=" ")
        
        try:
            start_time = time.time()
            response = requests.get(url, timeout=10)
            response_time = time.time() - start_time
            
            if response.status_code == 200:
                print(f"✓ 正常 ({response_time:.2f}s, {len(response.content)} bytes)")
                results.append(True)
                
                # 檢查主頁是否包含記憶體資訊
                if path == "/":
                    content = response.text
                    if "可用記憶體" in content or "Free Heap" in content:
                        print("  └─ 包含記憶體狀態資訊")
                    if "DaiSpan" in content:
                        print("  └─ 頁面標題正確")
            else:
                print(f"✗ HTTP {response.status_code}")
                results.append(False)
                
        except requests.exceptions.Timeout:
            print("✗ 超時")
            results.append(False)
        except requests.exceptions.ConnectionError:
            print("✗ 連接失敗")
            results.append(False)
        except Exception as e:
            print(f"✗ 錯誤: {e}")
            results.append(False)
    
    print("=" * 50)
    success_count = sum(results)
    total_count = len(results)
    success_rate = (success_count / total_count) * 100
    
    print(f"檢查結果: {success_count}/{total_count} 頁面正常 ({success_rate:.1f}%)")
    
    if success_rate >= 80:
        print("🟢 設備狀態：良好")
        return True
    elif success_rate >= 50:
        print("🟡 設備狀態：部分功能異常")
        return False
    else:
        print("🔴 設備狀態：嚴重異常")
        return False

def scan_daispan_device():
    """掃描可能的DaiSpan設備IP"""
    print("正在掃描 DaiSpan 設備...")
    
    # 常見的IP地址
    possible_ips = [
        "192.168.4.1",  # AP模式默認IP
        "192.168.1.100", 
        "192.168.1.101",
        "192.168.0.100",
        "192.168.0.101"
    ]
    
    for ip in possible_ips:
        print(f"嘗試 {ip}...", end=" ")
        try:
            response = requests.get(f"http://{ip}", timeout=3)
            if response.status_code == 200 and ("DaiSpan" in response.text or "thermostat" in response.text.lower()):
                print("✓ 找到 DaiSpan 設備!")
                return ip
            else:
                print("✗ 不是 DaiSpan")
        except:
            print("✗ 無回應")
    
    print("未找到 DaiSpan 設備")
    return None

if __name__ == "__main__":
    if len(sys.argv) > 1:
        device_ip = sys.argv[1]
    else:
        # 自動掃描設備
        device_ip = scan_daispan_device()
        if not device_ip:
            print("請手動指定設備IP: python3 quick_check.py <IP地址>")
            sys.exit(1)
    
    success = check_daispan_status(device_ip)
    sys.exit(0 if success else 1)