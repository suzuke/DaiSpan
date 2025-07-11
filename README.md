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

### **Advanced Remote Debugging** 🛠️
- 🌐 **WebSocket-based Remote Debugging** - Real-time monitoring without USB connection
- 📊 **Live Serial Log Streaming** - Equivalent to `pio device monitor` but accessible remotely
- 🔍 **System Diagnostics** - Comprehensive health checks and status monitoring
- 📈 **Performance Metrics** - Memory usage, WiFi signal, system uptime tracking
- 🎯 **HomeKit Operation Tracking** - Real-time logging of all HomeKit interactions
- 💻 **Web-based Interface** - Professional debugging dashboard accessible from any device

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

## 🛠️ **Hardware Requirements**

### **Supported ESP32 Boards**
| Board | Status | RX Pin | TX Pin | Flash Size | Notes |
|-------|--------|--------|--------|------------|-------|
| **ESP32-S3 DevKitC-1** | ✅ Primary | 14 | 13 | 16MB | Recommended |
| **ESP32-C3 SuperMini** | ✅ Tested | 4 | 3 | 4MB | Compact option |
| **ESP32-S3 SuperMini** | ✅ Supported | 13 | 12 | 16MB | Alternative |

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

# Upload to ESP32 (choose your board)
pio run -e esp32-s3-usb -t upload        # ESP32-S3 via USB
pio run -e esp32-c3-supermini-usb -t upload # ESP32-C3 via USB
```

### **2. Initial Setup**

1. **Power on** your ESP32 device
2. **Connect** to WiFi network "DaiSpan-Config"
3. **Navigate** to web interface (usually `192.168.4.1`)
4. **Configure** WiFi credentials and HomeKit settings
5. **Add** to iOS Home app using pairing code: `11122333`

### **3. Advanced Development**

```bash
# Multiple build environments
pio run -e esp32-s3-usb -t upload          # USB upload
pio run -e esp32-s3-ota -t upload          # OTA upload (IP: configured in platformio.ini)

# Monitoring and debugging
pio device monitor                          # Local serial monitoring
# Or use remote debugging at http://device-ip:8080/debug

# Testing and validation
python3 scripts/quick_check.py [device_ip] # Quick health check
python3 scripts/long_term_test.py 192.168.4.1 24 5  # 24-hour stability test
```

## 🌐 **Remote Debugging System**

One of DaiSpan's standout features is its comprehensive remote debugging capability:

### **Access the Remote Debugger**
```
http://your-device-ip:8080/debug
```

### **Features**
- 📡 **Real-time Serial Logs** - View all debug output remotely
- 📊 **System Status Monitoring** - Memory, WiFi, HomeKit status
- 🔍 **Live Diagnostics** - System health checks
- 📈 **Performance Metrics** - Real-time system performance data
- 🏠 **HomeKit Operation Tracking** - Monitor all HomeKit interactions
- 🎯 **Multi-client Support** - Multiple browsers can connect simultaneously

### **WebSocket Integration**
The debugging system uses WebSockets for real-time communication:
- **WebSocket Server**: `ws://device-ip:8081`
- **Protocol**: JSON-based command/response system
- **Commands**: `get_status`, `diagnostics`, `get_history`

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
- **Flash Usage**: 91.3% (1.53MB/1.69MB)
- **RAM Usage**: 16.1% (~140KB available)
- **Update Frequency**: 6-second status polling
- **Response Time**: <100ms for HomeKit operations

### **Memory Management**
- ✅ **Optimized Partitions** - Custom partition table for OTA
- ♻️ **Dynamic Memory** - Efficient memory allocation
- 📈 **Monitoring** - Real-time memory usage tracking

## 🧪 **Testing & Quality Assurance**

### **Automated Testing Scripts**
```bash
# Quick system validation
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
- 🔄 **Simulation Mode** - Mock controller for development

## 🛠️ **Configuration**

### **Hardware Pin Configuration**
Automatically configured based on board selection in `include/common/Config.h`:

```cpp
// ESP32-C3 SuperMini
#define S21_RX_PIN 4
#define S21_TX_PIN 3

// ESP32-S3 variants  
#define S21_RX_PIN 13  // or 14 for DevKitC-1
#define S21_TX_PIN 12  // or 13 for DevKitC-1
```

### **Operating Modes**
1. **Production Mode** - Full S21 communication with real AC unit
2. **Simulation Mode** - Mock implementation for testing and development

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
- **HomeKit not responding**: Check remote debug logs at `/debug`
- **WiFi connection issues**: Verify 2.4GHz network compatibility  
- **S21 communication errors**: Verify pin connections and protocol settings
- **Memory issues**: Monitor usage via remote debugging interface

### **Debug Resources**
- 🌐 **Remote Debug Interface**: `http://device-ip:8080/debug`
- 📡 **WebSocket Logs**: `ws://device-ip:8081`
- 📊 **System Diagnostics**: Built-in health check commands

## 📄 **License**

MIT License - see [LICENSE](LICENSE) for details.

This project incorporates code and concepts from:
- **HomeSpan** (Copyright © 2020-2024 Gregg E. Berman)
- **ESP32-Faikin** (Copyright © 2022 Adrian Kennard)

## 🙏 **Acknowledgments**

Special thanks to:
- 🏠 **HomeSpan Project** - Excellent HomeKit library
- 🌊 **ESP32-Faikin** - S21 protocol foundation  
- 🤖 **Claude Code** - Development assistance and remote debugging system design

---

<div align="center">

**Made with ❤️ for the Smart Home Community**

*For support, please use the remote debugging tools or check the documentation*

</div>