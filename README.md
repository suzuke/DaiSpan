# DaiSpan - Advanced Daikin S21 HomeKit Bridge

<div align="center">

![ESP32](https://img.shields.io/badge/ESP32-000000?style=flat&logo=espressif&logoColor=white)
![HomeKit](https://img.shields.io/badge/HomeKit-000000?style=flat&logo=apple&logoColor=white)
![PlatformIO](https://img.shields.io/badge/PlatformIO-FF7F00?style=flat&logo=platformio&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-blue.svg)

*A professional-grade HomeKit bridge for Daikin air conditioners with comprehensive remote debugging capabilities*

[繁體中文版](README_TW.md) | [Documentation](CLAUDE.md)

</div>

## 🌟 **Key Features**

### **HomeKit Integration**
- ✅ **Full HomeKit Compatibility** - Native iOS Home app support
- 🌡️ **Temperature Control** - Precise temperature monitoring and adjustment (16°C - 30°C)
- 🔄 **Operation Modes** - Heat, Cool, Auto, Fan, Dry modes
- 💨 **Fan Speed Control** - Multiple speed levels with auto mode
- ⚡ **Real-time Updates** - Instant status synchronization
- 📱 **Siri Integration** - Voice control support

### **Protocol & Hardware Support**
- 🔌 **Multiple ESP32 Variants** - ESP32-S3, ESP32-C3 SuperMini support
- 📡 **S21 Protocol Versions** - Full support for 1.0, 2.0, and 3.xx
- 🔄 **Auto Protocol Detection** - Automatic version detection and optimization
- 🏗️ **Modular Architecture** - Extensible design for future AC brands
- ⚙️ **OTA Updates** - Over-the-air firmware updates

### **Web Interface & Management**
- 🌐 **Comprehensive Web Dashboard** - Status monitoring and configuration
- 🛜 **WiFi Management** - Easy setup and credential management  
- 🔧 **Configuration Interface** - HomeKit settings and device management
- 🇹🇼 **Chinese Language Support** - Full Traditional Chinese interface
- 📊 **Real-time Monitoring** - Live system status and performance metrics

## 🔄 **Recent Architecture Updates**

- 🧾 **HomeKit Command Queue with Confirmation** – Mode and temperature changes are queued and applied only after the indoor unit confirms success, preventing HomeKit/UI desynchronization.
- 🚦 **Memory Pressure Levels** – The dashboard and `/api/health` expose Normal / Tight / Critical indicators so you can see when the system is throttling background work.
- ⏱️ **Scheduler Load Metrics** – Latest task count and execution time (μs) are displayed to help diagnose loop bottlenecks on constrained ESP32-C3 hardware.
- 🧹 **Legacy System Cleanup** – Removed unused event bus and service container layers to lower memory footprint while keeping HomeKit, Wi-Fi provisioning, and OTA as the core focus.
## 🛠️ **Hardware Requirements**

### **Supported ESP32 Boards**
| Board | Status | RX Pin | TX Pin | Flash Size | Notes |
|-------|--------|--------|--------|------------|-------|
| **ESP32-C3 SuperMini** | ✅ Primary | 4 | 3 | 4MB | Minimal HomeKit bridge target |

### **Additional Hardware**
- 🔌 **TTL to S21 Adapter** (3.3V level)
- 🏠 **Daikin Air Conditioner** with S21 port
- 📶 **Stable WiFi Connection** (2.4GHz)

## 🚀 **Quick Start**

### **1. Installation**

```bash
# Clone the repository
git clone https://github.com/your-username/DaiSpan.git
cd DaiSpan

# Install PlatformIO (if not installed)
pip install platformio

# Build firmware
pio run

# Upload to ESP32-C3 SuperMini
pio run -e esp32-c3-supermini -t upload
```

### **2. Initial Setup**

1. **Power on** your ESP32 device
2. **Connect** to WiFi network "DaiSpan-Config"
3. **Navigate** to web interface (usually `192.168.4.1`)
4. **Configure** WiFi credentials and HomeKit settings
5. **Add** to iOS Home app using pairing code: `11122333`

### **3. Advanced Development**

```bash
# Minimal production build (default)
pio run -e esp32-c3-supermini -t upload

# Optional: enable OTA support (requires updating upload_port/IP)
pio run -e esp32-c3-supermini-ota -t upload

# Serial monitor for日誌除錯
pio device monitor

# 基本驗證腳本
python3 scripts/quick_check.py [device_ip]
python3 scripts/long_term_test.py [device_ip] 24 5
```

## 🏗️ **Architecture**

DaiSpan follows a clean, modular architecture:

```
┌─ Device Layer (HomeKit Integration)
├─ Controller Layer (Business Logic)  
├─ Protocol Layer (S21 Communication)
└─ Common Layer (Utilities & Config)
```

### **Key Components**
- 🏭 **Protocol Factory** - Extensible AC protocol support
- 🔄 **Adapter Pattern** - Clean protocol abstraction
- 🎯 **Dependency Injection** - Modular, testable design
- 🛡️ **Error Recovery** - Robust error handling and recovery

## 📊 **Performance & Memory**

### **Current Metrics (ESP32-C3)**
- **Flash Usage**: 77.0% (1.56MB / 2.03MB)
- **RAM Usage**: 22.4% (~73KB used)
- **Update Frequency**: 6-second status polling
- **Response Time**: <100ms for HomeKit operations

### **Memory Management**
- ✅ **Optimized Partitions** - Custom partition table for OTA
- ♻️ **Dynamic Memory** - Efficient memory allocation
- 📈 **Unified Memory Monitoring** - Health status indicators across all build environments
- 🧠 **Hardware-Aware Profiles** - Tuned thresholds/buffer pools for ESP32-C3 minimal vs OTA builds
- 🚨 **Memory Health System** - EXCELLENT/GOOD/WARNING/CRITICAL/EMERGENCY status levels
- 🔧 **Adaptive Memory Strategies** - Automatic optimization based on available resources

## 🧪 **Testing & Quality Assurance**

### **Automated Testing Scripts**
```bash
# Quick system validation (includes memory profile check)
python3 scripts/quick_check.py [device_ip]

# Long-term stability testing  
python3 scripts/long_term_test.py [device_ip] [hours] [interval_minutes]

# Resource monitoring
./scripts/resource_monitor.sh [device_ip] [interval_seconds]
```

### **Testing Strategy**
- 🔬 **Unit Testing** - Protocol abstraction enables isolated testing
- 🏃 **Integration Testing** - Web interface manual testing
- 📊 **Performance Testing** - Long-term stability validation

## 🛠️ **Configuration**

### **Hardware Pin Configuration**
Automatically configured based on board selection in `include/common/Config.h`:

```cpp
// ESP32-C3 SuperMini（唯一支援板）
#define S21_RX_PIN 4
#define S21_TX_PIN 3
```

### **Operating Modes**
1. **Production Mode** - Full S21 communication with real AC unit
2. **Optional OTA Mode** - Enable via `ENABLE_OTA_UPDATE` when remote flashing is required

## 🤝 **Contributing**

We welcome contributions! Please see our contribution guidelines:

1. 🍴 **Fork** the repository
2. 🌿 **Create** a feature branch
3. ✅ **Test** your changes thoroughly
4. 📝 **Document** new features
5. 🔄 **Submit** a pull request

## 📚 **Documentation**

- 📖 **[CLAUDE.md](CLAUDE.md)** - Comprehensive development guide
- 🏗️ **[MAIN_LOOP_REFACTORING.md](MAIN_LOOP_REFACTORING.md)** - Architecture decisions
- 🧪 **[scripts/README.md](scripts/README.md)** - Testing tools documentation

## 🐛 **Troubleshooting**

### **Common Issues**
- **HomeKit not responding**: Inspect serial logs via `pio device monitor`
- **WiFi connection issues**: Verify 2.4GHz network compatibility  
- **S21 communication errors**: Verify pin connections and protocol settings
- **Memory issues**: Query `/api/memory/stats` 或 `/api/memory/detailed` 查看壓力指標

### **Debug Resources**
- 🌐 **Web Interface**: `http://device-ip:8080`
- 📝 **Health APIs**: `/api/memory/stats`, `/api/memory/detailed`, `/api/monitor/dashboard`
- 🧰 **Serial Log**: `pio device monitor` for real-time debugging

## 📄 **License**

MIT License - see [LICENSE](LICENSE) for details.

This project incorporates code and concepts from:
- **HomeSpan** (Copyright © 2020-2024 Gregg E. Berman)
- **ESP32-Faikin** (Copyright © 2022 Adrian Kennard)

## 🙏 **Acknowledgments**

Special thanks to:
- 🏠 **HomeSpan Project** - Excellent HomeKit library
- 🌊 **ESP32-Faikin** - S21 protocol foundation  
- 🤖 **Claude Code** - Development assistance across refactors

---

<div align="center">

**Made with ❤️ for the Smart Home Community**

*For support, please monitor serial logs or參考文件*

</div>
