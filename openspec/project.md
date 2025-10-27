# Project Context

## Purpose
DaiSpan is an ESP32-based HomeKit bridge that speaks the Daikin S21 protocol, exposing thermostat, fan, and diagnostics controls to Apple HomeKit with a lightweight web interface for configuration and status checks. The project targets professional-grade reliability with modular architecture, Chinese-localized UX, and OTA-capable firmware packages for multiple ESP32 variants.

## Tech Stack
- PlatformIO (`espressif32` platform) with Arduino framework and C++17 toolchain
- ESP32-C3 SuperMini（單一支援硬體）
- HomeSpan library for HomeKit integration and accessory modeling
- ArduinoJson, DNSServer, and custom web server components
- Embedded web UI rendered via optimized HTML templates with Traditional Chinese content
- Python 3 tooling (`requests`) and shell scripts under `scripts/` for validation and monitoring

## Project Conventions

### Code Style
- Modern C++17 with exceptions disabled; prefer smart pointers (`std::unique_ptr`) and RAII
- Four-space indentation, PascalCase types (`SystemManager`), camelCase functions (`initializeMonitoring`), and ALL_CAPS macros/build flags
- Keep headers self-contained, leverage inline documentation (often Traditional Chinese) for complex flows, and gate features with `#ifdef` build flags
- Avoid dynamic RTTI; rely on templates and explicit type registries in the core layer

### Architecture Patterns
- Layered layout: `core` (event system & service container) → `domain` (aggregates/value objects) → `controller` (business orchestration) → `device` (HomeSpan bindings) → `common` (infrastructure utilities)
- Event-driven processing with an RTTI-free publisher/subscriber bus and dependency-injection container for service lookup
- Protocol abstraction via `IACProtocol` implementations and `ACProtocolFactory` for S21 variants and future HVAC protocols
- Unified web interface (port 8080) backed by memory-optimized template rendering；聚焦必要頁面（主頁、Wi-Fi、HomeKit、可選 OTA）
- Build-time flags 簡化為核心宏（例如 `PRODUCTION_BUILD`, `ENABLE_OTA_UPDATE`），避免互斥的調試/模擬設定

### Testing Strategy
- Compile and upload firmware with PlatformIO environment targets (`pio run -e <env>`); primary default is `esp32-c3-supermini`
- Use hardware-in-the-loop validation guided by `TEST_PLAN.md`, focusing on web server endpoints, memory pressure handling, and HomeKit flows
- Python and shell scripts in `scripts/` (`quick_check.py`, `long_term_test.py`, `resource_monitor.sh`, `performance_test.py`) provide automated smoke, endurance, and telemetry checks; require `requests`, `curl`, `bc`

### Git Workflow
- Follow OpenSpec process: review existing specs, scaffold change proposals for new capabilities, and validate with `openspec validate --strict` before implementation
- Develop on short-lived feature branches off the main branch; reference the associated `change-id` in commit messages when applicable
- Keep commits focused, update documentation (README, CLAUDE.md, specs) alongside code, and ensure `pio run` plus relevant scripts pass before opening review
- Production-ready builds should be generated from the `esp32-c3-supermini-production` or corresponding hardware environment after successful testing

## Domain Context
- Targets Daikin air conditioners using the S21 serial protocol (versions 1.x–3.x) with automatic version detection and adapter-style protocol handling
- Bridges HVAC capabilities—temperature, mode, fan speed, status telemetry—to Apple HomeKit via HomeSpan accessories and Siri integration
- Provides a bilingual (Traditional Chinese/English) configuration與診斷入口（WiFi 配網、HomeKit 設定、核心狀態、可選 OTA）
- Focuses on單一生產模式：移除模擬與遠端除錯，透過序列埠 log 搭配 `/api/memory/*` 進行觀測

## Important Constraints
- ESP32 resource limits: ESP32-C3 flash 4 MB & RAM ~320 KB, ESP32-S3 flash 16 MB; firmware typically consumes >80% flash, so additions must be memory-conscious
- Custom partition table (`partitions_custom.csv`) required for OTA; ensure new data fits within allocated OTA and SPIFFS partitions
- All features must operate on 2.4 GHz WiFi networks with intermittent connectivity; avoid blocking code paths in main loop
- 預設僅開啟必要功能；如需 OTA 更新，需顯式透過建置旗標啟用並評估記憶體負擔
- Maintain compatibility with both USB and OTA workflows; respect build flags controlling mock controllers and resource-heavy components

## External Dependencies
- Arduino libraries from PlatformIO registry: HomeSpan, ArduinoJson, DNSServer, links2004/WebSockets, plus ESP32 Arduino core
- Daikin HVAC hardware with S21 connector and TTL-level adapter hardware for deployment
- Web browser clients hitting device endpoints (`/`, `/wifi`, `/homekit`, `/api/*`, `/debug`) and HomeKit ecosystem devices (iOS Home app, Siri)
- Development tooling: Python 3 with `requests`, macOS/Linux utilities (`curl`, `bc`), and PlatformIO CLI
