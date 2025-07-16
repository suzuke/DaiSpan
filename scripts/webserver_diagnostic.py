#!/usr/bin/env python3
"""
DaiSpan WebServer Diagnostic Tool
檢查 webserver 狀態和連接問題
"""

import requests
import socket
import sys
import time
import subprocess
import json
from typing import Dict, Any, Optional, List

class DaiSpanDiagnostic:
    def __init__(self, ip: str = "192.168.50.192"):
        self.ip = ip
        self.web_port = 8080
        self.debug_port = 8081
        self.config_port = 80
        
    def check_network_connectivity(self) -> bool:
        """檢查基本網路連接"""
        print(f"📡 檢查網路連接到 {self.ip}...")
        try:
            result = subprocess.run(['ping', '-c', '3', self.ip], 
                                  capture_output=True, text=True, timeout=30)
            if result.returncode == 0:
                print("✅ 設備網路可達")
                return True
            else:
                print("❌ 設備無法連接")
                return False
        except Exception as e:
            print(f"❌ 網路檢查失敗: {e}")
            return False
    
    def scan_ports(self) -> Dict[int, bool]:
        """掃描常用端口"""
        print(f"🔍 掃描 {self.ip} 的端口...")
        ports = [80, 443, 8080, 8081, 8082, 8083, 3000, 5000, 9000]
        results = {}
        
        for port in ports:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(3)
            try:
                result = sock.connect_ex((self.ip, port))
                results[port] = result == 0
                status = "開放" if result == 0 else "關閉"
                print(f"  - 端口 {port}: {status}")
            except Exception as e:
                results[port] = False
                print(f"  - 端口 {port}: 錯誤 ({e})")
            finally:
                sock.close()
        
        return results
    
    def check_webserver_status(self) -> Optional[Dict[str, Any]]:
        """檢查 webserver 狀態"""
        print(f"🌐 檢查 WebServer 狀態...")
        
        # 嘗試不同的端口和路徑
        test_urls = [
            f"http://{self.ip}:{self.web_port}/",
            f"http://{self.ip}:{self.web_port}/api/health",
            f"http://{self.ip}:{self.config_port}/",
            f"http://{self.ip}/",
        ]
        
        for url in test_urls:
            try:
                print(f"  - 嘗試連接: {url}")
                response = requests.get(url, timeout=10)
                if response.status_code == 200:
                    print(f"✅ WebServer 響應正常 (狀態碼: {response.status_code})")
                    try:
                        return response.json()
                    except:
                        return {"status": "ok", "content": response.text[:200]}
                else:
                    print(f"⚠️ WebServer 響應異常 (狀態碼: {response.status_code})")
            except requests.exceptions.ConnectError:
                print(f"❌ 無法連接到 {url}")
            except requests.exceptions.Timeout:
                print(f"⏱️ 連接超時: {url}")
            except Exception as e:
                print(f"❌ 連接錯誤: {e}")
        
        return None
    
    def check_configuration_mode(self) -> bool:
        """檢查是否處於配置模式"""
        print("🔧 檢查配置模式...")
        
        # 檢查是否有 DaiSpan-Config AP
        try:
            result = subprocess.run(['iwlist', 'scan'], capture_output=True, text=True, timeout=30)
            if "DaiSpan-Config" in result.stdout:
                print("✅ 發現 DaiSpan-Config AP - 設備處於配置模式")
                return True
        except:
            pass
        
        # 檢查是否在配置網段
        if self.ip.startswith("192.168.4."):
            print("✅ 設備可能處於配置模式 (192.168.4.x 網段)")
            return True
        
        print("❌ 設備不在配置模式")
        return False
    
    def get_diagnostic_recommendations(self, port_results: Dict[int, bool]) -> List[str]:
        """生成診斷建議"""
        recommendations = []
        
        if not any(port_results.values()):
            recommendations.extend([
                "🔄 重啟設備",
                "🔌 檢查電源和硬件連接",
                "📱 確認 HomeKit 配對狀態",
                "💾 檢查是否有足夠的內存 (需要 >70KB)",
            ])
        
        if not port_results.get(8080, False):
            recommendations.extend([
                "⏱️ 等待 HomeKit 初始化完成",
                "🔍 檢查系統管理器狀態",
                "🏠 確認 HomeKit 穩定運行",
                "📊 監控系統內存使用情況",
            ])
        
        if self.check_configuration_mode():
            recommendations.extend([
                "🌐 連接到 DaiSpan-Config WiFi",
                "🔗 訪問 http://192.168.4.1 進行配置",
                "📋 完成 WiFi 和 HomeKit 設定",
            ])
        
        return recommendations
    
    def run_full_diagnostic(self):
        """運行完整診斷"""
        print("🔍 DaiSpan WebServer 診斷工具")
        print("=" * 50)
        
        # 1. 網路連接檢查
        if not self.check_network_connectivity():
            print("❌ 基本網路連接失敗，請檢查設備電源和網路")
            return
        
        # 2. 端口掃描
        port_results = self.scan_ports()
        
        # 3. WebServer 狀態檢查
        webserver_status = self.check_webserver_status()
        
        # 4. 配置模式檢查
        config_mode = self.check_configuration_mode()
        
        # 5. 生成建議
        print("\n💡 診斷建議:")
        print("-" * 30)
        recommendations = self.get_diagnostic_recommendations(port_results)
        for i, rec in enumerate(recommendations, 1):
            print(f"{i}. {rec}")
        
        # 6. 總結
        print("\n📋 診斷總結:")
        print("-" * 30)
        if webserver_status:
            print("✅ WebServer 正常運行")
        elif any(port_results.values()):
            print("⚠️ 部分服務正在運行，但 WebServer 未啟動")
        else:
            print("❌ 沒有檢測到任何服務")
        
        if config_mode:
            print("🔧 設備可能處於配置模式，需要完成初始設定")
        else:
            print("🏠 設備應該處於正常運行模式")

if __name__ == "__main__":
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.50.192"
    diagnostic = DaiSpanDiagnostic(ip)
    diagnostic.run_full_diagnostic()