# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DaiSpan is an ESP32-based HomeKit smart thermostat that controls Daikin air conditioners via the S21 protocol. The project features a modern event-driven architecture with RTTI-free design, domain-driven patterns, and comprehensive monitoring capabilities. The system provides excellent performance and maintainability through clean abstractions and dependency injection.

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
The default environment targets `esp32-c3-supermini`.
- Minimal build: `pio run -e esp32-c3-supermini`
- Optional OTA build: `pio run -e esp32-c3-supermini-ota`

## Architecture Overview

### Architecture Structure
The codebase follows a modern event-driven architecture:

1. **Core Architecture** (`include/core/`): Event-driven foundation
   - `EventSystem.h`: RTTI-free template-based event system
   - `ServiceContainer.h`: String-based dependency injection container
   - `Result.h`: Functional error handling
   - `TypeRegistry.h`: Type system utilities

2. **Domain Layer** (`include/domain/`): Business logic
   - `ThermostatDomain.h`: Domain aggregates and value objects
   - `ConfigDomain.h`: Configuration domain logic

3. **Protocol Layer** (`include/protocol/`): Hardware communication abstractions
   - `IACProtocol.h`: Generic AC control interface
   - `S21Protocol.h`: Daikin S21 implementation
   - `ACProtocolFactory.h`: Protocol factory for extensibility

4. **Controller Layer** (`include/controller/`): Business logic adapters
   - `IThermostatControl.h`: Control interface
   - `ThermostatController.h`: Main controller implementation

5. **Device Layer** (`include/device/`): HomeKit integration
   - `ThermostatDevice.h`: HomeSpan-based HomeKit thermostat

6. **Common Layer** (`include/common/`): Shared utilities
   - `Config.h`: Configuration management
   - `WiFiManager.h`: Network handling
   - `LogManager.h`: Logging system

### Key Design Patterns
- **Event-Driven Architecture**: Asynchronous event processing with RTTI-free implementation
- **Dependency Injection**: Service container with factory pattern support
- **Domain-Driven Design**: Aggregates, value objects, and domain events
- **Functional Error Handling**: Result<T> pattern for error management
- **Factory Pattern**: `ACProtocolFactory` creates protocol instances
- **Adapter Pattern**: `S21ProtocolAdapter` bridges protocol specifics
- **Interface Segregation**: Clean abstractions between layers

## Configuration and Setup

### Hardware Configuration
The system targets ESP32-C3 SuperMini (RX=4, TX=3).
Pin configurations are defined in `include/common/Config.h`.

### Operating Modes
- **Production Mode**: Communicates with actual Daikin AC via S21 protocol
- **Optional OTA Mode**: Enable via `ENABLE_OTA_UPDATE` when remote flashing is needed

Mode selection is controlled via build flags and the web interface.

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
- System status monitoring with real-time metrics
- WiFi configuration and network management
- HomeKit settings management
- Memory and health APIs (`/api/memory/stats`, `/api/monitor/dashboard`)
- Optional OTA update入口（若啟用 `ENABLE_OTA_UPDATE`）
- Chinese language support

### API Endpoints
- `/api/health` - System health check with component status
- `/api/metrics` - Comprehensive performance metrics including memory, CPU, network, and HomeKit status
- `/api/logs` - System logs with component filtering and severity levels
- `/api/core/stats` - Core architecture statistics with event system metrics
- `/api/core/reset` - Reset event system statistics
- `/core-test-event` - Manual event testing endpoint

## Memory Constraints

Current usage (ESP32-C3 minimal build):
- Flash: 77.0% (1.56MB/2.03MB)
- RAM: 22.4% (73KB/327KB)

Keep these limits in mind when adding features. Optional OTA builds consume slightly more flash/RAM.

### Memory Profiles
- `MemoryProfile` 模組現聚焦於 ESP32-C3，區分最小與 OTA 兩種 profile。
- `MemoryOptimization::MemoryManager`、`BufferPool`、`StreamingResponseBuilder` 會讀取配置檔；可透過 `/api/memory/*` 與 `scripts/quick_check.py` 查詢當前設定。
- 如需調整，請更新 `src/MemoryProfile.cpp` 中的 C3 profile 定義。

## HomeKit Integration

Uses HomeSpan library for HomeKit compatibility:
- Temperature range: 16°C - 30°C
- Modes: Off, Heat, Cool, Auto
- Default pairing code: 11122333
- Configurable device name and manufacturer info

## Logging System

The project uses an optimized logging system with multiple levels:
- `DEBUG_ERROR` - Critical errors only
- `DEBUG_WARN` - Warning messages
- `DEBUG_INFO` - Important status updates (default)
- `DEBUG_VERBOSE` - Detailed communication traces

Features:
- Static buffer allocation for memory efficiency
- Remote web log integration
- Production build optimization
- Component-based filtering

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
