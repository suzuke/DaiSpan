# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DaiSpan is an ESP32-based HomeKit smart thermostat that controls Daikin air conditioners via the S21 protocol. The project features a modern layered architecture with protocol abstraction, allowing future support for multiple AC brands.

## Development Commands

### Build and Deploy
```bash
# Build firmware (default target: ESP32-C3 SuperMini)
pio run

# Upload via USB (default method)
pio run -t upload

# Upload via OTA
pio run -e esp32-c3-supermini-ota -t upload

# Monitor serial output
pio device monitor

# Clean build
pio run -t clean
```

### Environment Configuration
The project supports flexible upload methods via different environments:

#### USB Upload (Default)
```bash
# Uses esp32-s3-usb environment (default)
pio run -e esp32-s3-usb -t upload
```

#### OTA Upload
```bash
# Uses esp32-s3-ota environment
pio run -e esp32-s3-ota -t upload
```

**Note**: For OTA uploads, ensure the device IP address is correctly set in `platformio.ini` under the respective OTA environment section. The default OTA authentication password is `12345678`.

### Multi-Board Support

#### ESP32-S3 DevKitC-1 (Primary Target)
```bash
# USB Upload
pio run -e esp32-s3-usb -t upload

# OTA Upload  
pio run -e esp32-s3-ota -t upload

# Legacy environment (build only)
pio run -e esp32-s3-devkitc-1-n16r8v
```

#### ESP32-C3 SuperMini
```bash
# USB Upload
pio run -e esp32-c3-supermini-usb -t upload

# OTA Upload
pio run -e esp32-c3-supermini-ota -t upload

# Legacy environment (build only)
pio run -e esp32-c3-supermini
```

#### ESP32-S3 SuperMini
```bash
# USB Upload
pio run -e esp32-s3-supermini-usb -t upload

# OTA Upload
pio run -e esp32-s3-supermini-ota -t upload

# Legacy environment (build only)
pio run -e esp32-s3-supermini
```

## Architecture Overview

### Layer Structure
The codebase follows a clean layered architecture:

1. **Protocol Layer** (`include/protocol/`): Hardware communication abstractions
   - `IACProtocol.h`: Generic AC control interface
   - `S21Protocol.h`: Daikin S21 implementation
   - `ACProtocolFactory.h`: Protocol factory for extensibility

2. **Controller Layer** (`include/controller/`): Business logic
   - `IThermostatControl.h`: Control interface
   - `ThermostatController.h`: Main controller implementation
   - `MockThermostatController.h`: Simulation mode

3. **Device Layer** (`include/device/`): HomeKit integration
   - `ThermostatDevice.h`: HomeSpan-based HomeKit thermostat

4. **Common Layer** (`include/common/`): Shared utilities
   - `Config.h`: Configuration management
   - `WiFiManager.h`: Network handling
   - `LogManager.h`: Logging system

### Key Design Patterns
- **Factory Pattern**: `ACProtocolFactory` creates protocol instances
- **Adapter Pattern**: `S21ProtocolAdapter` bridges protocol specifics
- **Interface Segregation**: Clean abstractions between layers
- **Dependency Injection**: Controllers accept protocol interfaces

## Configuration and Setup

### Hardware Configuration
The system supports multiple ESP32 variants with different pin assignments:
- ESP32-C3 Super Mini: RX=4, TX=3
- ESP32-S3 Super Mini: RX=13, TX=12
- ESP32-S3 DevKitC-1: RX=14, TX=13

Pin configurations are defined in `include/common/Config.h`.

### Operating Modes
1. **Real Hardware Mode**: Communicates with actual Daikin AC via S21 protocol
2. **Simulation Mode**: Mock implementation for testing and development

Mode selection is controlled via web interface or compile-time flags.

### Initial Setup Process
1. Device creates "DaiSpan-Config" WiFi access point
2. Connect to AP and navigate to web interface
3. Configure WiFi credentials and HomeKit settings
4. Device switches to operational mode with HomeKit pairing

## Adding New AC Protocols

The architecture is designed for easy protocol extension:

1. Create new protocol class implementing `IACProtocol` interface
2. Add protocol type to `ACProtocolType` enum in `include/common/ThermostatMode.h`
3. Register in `ACProtocolFactory::createProtocol()`
4. Implement detection logic in factory
5. No changes needed to controller or device layers

## Web Interface Features

The built-in web server provides:
- System status monitoring
- WiFi configuration
- HomeKit settings management
- Simulation mode controls
- OTA update capability
- Chinese language support

## Memory Constraints

Current usage (ESP32-C3):
- Flash: 75.3% (1.5MB/2MB)
- RAM: 15.9% (52KB/327KB)

Keep these limits in mind when adding features. The custom partition table reserves space for OTA updates.

## HomeKit Integration

Uses HomeSpan library for HomeKit compatibility:
- Temperature range: 16°C - 30°C
- Modes: Off, Heat, Cool, Auto
- Default pairing code: 11122333
- Configurable device name and manufacturer info

## Testing Strategy

- Mock controller enables simulation mode testing
- Protocol abstraction allows unit testing of business logic
- Web interface provides manual testing capabilities
- Serial monitor shows detailed debug logs with Chinese descriptions

### Testing and Monitoring Scripts
The `scripts/` directory contains Python and shell tools for device testing:

```bash
# Quick device status check
python3 scripts/quick_check.py [device_ip]

# Long-term stability testing (24 hours, check every 5 minutes)
python3 scripts/long_term_test.py 192.168.4.1 24 5

# Continuous background monitoring
./scripts/resource_monitor.sh 192.168.4.1 [interval_seconds]
```

**Dependencies**: Install Python `requests` library and system tools (`bc`, `curl`)