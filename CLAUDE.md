# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DaiSpan is an ESP32-based HomeKit smart thermostat that controls Daikin air conditioners via the S21 serial protocol. Two versions exist:

- **v1** (root): PlatformIO + Arduino + C++17 + HomeSpan library
- **v2** (`v2/`): ESP-IDF + esp-homekit-sdk + FreeRTOS (C, recommended for new development)

## Build and Deploy

### v2 (ESP-IDF)

```bash
cd v2
source ~/esp/esp-idf/export.sh
idf.py build                    # Build
idf.py flash                    # Flash via USB
idf.py monitor                  # Serial monitor
# Or OTA: upload .bin via http://device-ip:8080/ota
```

Requires ESP-IDF v5.3+ and esp-homekit-sdk at `~/esp/esp-homekit-sdk`.

### v1 (PlatformIO)

```bash
# Build (default: esp32-c3-supermini)
pio run

# Upload via USB (pick your board env)
pio run -e esp32-c3-supermini-usb -t upload
pio run -e esp32-s3-usb -t upload
pio run -e esp32-s3-supermini-usb -t upload

# Upload via OTA (set IP in platformio.ini first, auth: 12345678)
pio run -e esp32-c3-supermini-ota -t upload

# Production build (no simulation/mock, smaller binary)
pio run -e esp32-c3-supermini-production -t upload

# Monitor serial output
pio device monitor

# Clean
pio run -t clean
```

**Environment naming pattern**: `esp32-{chip}-{board}-{method}` where method is `usb`, `ota`, or `production`.

## Architecture

### v2 Architecture (ESP-IDF)

```
HomeKit (HAP) ──write──> FreeRTOS Queue ──> Controller Task ──> S21 UART
HomeKit (HAP) <──read──  cached_status   <── Controller Task <── S21 UART
Web Server (port 8080): status, WiFi config, OTA upload, health/metrics API
```

Key design: Controller task (priority 5) owns all UART I/O. HomeKit reads from lock-free cached state and sends commands via non-blocking queue. On single-core ESP32-C3, struct copy is atomic.

Source files: `v2/main/` with subdirectories: `s21/`, `controller/`, `homekit/`, `wifi/`, `web/`, `config/`, `common/`.

### v1 Architecture (PlatformIO)

The system follows a layered architecture:

```
Device Layer (HomeKit via HomeSpan)
    |
Controller Layer (IThermostatControl interface)
    |--- ThermostatController (real hardware)
    |--- MockThermostatController (simulation, compile-guarded)
    |
Protocol Layer (IACProtocol interface)
    |--- S21Protocol -> S21ProtocolAdapter
    |--- ACProtocolFactory (creates protocol by type)
    |
Common Layer
    |--- ConfigManager (Preferences-backed with cache)
    |--- WiFiManager, OTAManager, SystemManager
    |--- WebUI + StreamingResponse (config/debug web interface)
    |--- RemoteDebugger (WebSocket on port 8081)
    |--- LogManager (level-based: ERROR/WARN/INFO/VERBOSE)
```

**Data flow**: HomeKit characteristic change -> ThermostatDevice -> IThermostatControl -> IACProtocol -> S21 serial (2400 baud) -> Daikin AC

**Key entry point**: `src/main.cpp` contains `setup()` and `loop()`, all global state, and web route handlers (~1200 lines).

### Important Compile Guards

Defined in platformio.ini build_flags per environment:

- `PRODUCTION_BUILD` — enables production optimizations
- `DISABLE_SIMULATION_MODE` — removes simulation mode code
- `DISABLE_MOCK_CONTROLLER` — removes mock controller code
- `ESP32C3_SUPER_MINI` / `ESP32S3_SUPER_MINI` — selects pin configuration

### Hardware Pin Mapping

Defined in `src/main.cpp`:
- ESP32-C3 SuperMini: RX=4, TX=3
- ESP32-S3 SuperMini: RX=13, TX=12
- ESP32-S3 DevKitC-1: RX=14, TX=13

## Adding New AC Protocols

1. Implement `IACProtocol` interface (`include/protocol/IACProtocol.h`)
2. Add type to `ACProtocolType` enum in `include/common/ThermostatMode.h`
3. Register in `ACProtocolFactory::createProtocol()`
4. Controller and device layers need no changes

## Memory Constraints

ESP32-C3 has tight limits (~2MB flash, ~327KB RAM). Flash usage is ~80%, RAM ~47%. Avoid adding large features without checking `pio run` output for size. The custom partition table (`partitions_custom.csv`) reserves space for OTA.

## Web Interface and Debugging

- Web config UI: `http://device-ip:8080`
- Remote debug dashboard: `http://device-ip:8080/debug`
- WebSocket real-time logs: `ws://device-ip:8081`
- API endpoints: `/api/health`, `/api/metrics`, `/api/memory/stats`

## Testing

No unit test framework — testing is done via:
- Simulation mode (mock controller) through web interface
- Serial monitor (`pio device monitor`)
- Python scripts in `scripts/` for device health checks and stability tests:
  ```bash
  python3 scripts/quick_check.py [device_ip]
  python3 scripts/long_term_test.py 192.168.4.1 24 5
  ```

## Configuration

Stored in ESP32 NVR via `Preferences` library (namespace: `daispan`). `ConfigManager` in `include/common/Config.h` provides cached access. WiFi setup happens through AP mode ("DaiSpan-Config" network). Default HomeKit pairing code: `11122333`.
