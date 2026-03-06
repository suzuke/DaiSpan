# DaiSpan v3 Design: Rust + ESP-IDF + Matter + S21

## Goal

Rewrite DaiSpan from scratch in Rust targeting ESP32-C3 and ESP32-S3, using Matter (via rs-matter) instead of HomeKit, with full S21 protocol support (v1~v3.50+, FX extended commands).

## Constraints

- Daikin S21 only (no multi-brand)
- ESP32-C3 (RISC-V, stable Rust) + ESP32-S3 (Xtensa, espup toolchain)
- 4MB flash, ~300KB RAM budget
- Pure Rust — no C FFI for application logic
- Matter over WiFi (no Thread)

## Architecture

```
iPhone Home App (Matter over WiFi)
        |
        v
+----------------------------------+
|  Matter Stack (rs-matter)        |
|  - Thermostat Cluster            |
|  - Fan Cluster                   |
|  - On/Off Cluster (swing)        |
+----------------------------------+
|  Controller Task                 |
|  - Command channel (async)       |
|  - Cached state (atomic/lock-free)|
|  - Dirty flags + error recovery  |
|  - Mode/fan protection timers    |
+----------------------------------+
|  S21 Protocol                    |
|  - UART driver (2400 baud 8E2)   |
|  - Frame codec (STX/ETX/checksum)|
|  - Version detection (v1~v3.50+) |
|  - FX extended commands          |
+----------------------------------+
|  Platform Services               |
|  - WiFi (STA + AP fallback)      |
|  - NVS config                    |
|  - Web server (WiFi/OTA/metrics) |
|  - mDNS                         |
+----------------------------------+
       ESP-IDF (via esp-idf-svc)
```

## Data Flow

```
Matter write callback
  -> async channel (non-blocking, capacity 8)
  -> Controller task
  -> S21 UART command
  -> ACK/NAK

S21 UART poll (every 6s)
  -> Controller task updates local_status
  -> Atomic copy to cached_status
  -> Matter read callback returns cached value
  -> Matter push notification to iPhone (on change)
```

## Module Structure

```
v3/
├── Cargo.toml
├── sdkconfig.defaults
├── partitions.csv
├── build.rs
└── src/
    ├── main.rs              # Entry point, task spawning
    ├── s21/
    │   ├── mod.rs
    │   ├── codec.rs         # Frame encode/decode, checksum
    │   ├── commands.rs      # Command definitions (D1/F1/RH/D5/FX)
    │   ├── uart.rs          # UART driver wrapper
    │   └── version.rs       # Version detection state machine
    ├── controller/
    │   ├── mod.rs
    │   ├── state.rs         # ac_status, cached state, dirty flags
    │   └── task.rs          # FreeRTOS task: channel -> S21 -> cache
    ├── matter/
    │   ├── mod.rs
    │   ├── thermostat.rs    # Thermostat cluster
    │   ├── fan.rs           # Fan cluster
    │   └── swing.rs         # On/Off cluster for swing
    ├── wifi/
    │   ├── mod.rs
    │   └── ap.rs            # AP mode + DNS captive portal
    ├── web/
    │   ├── mod.rs
    │   ├── routes.rs        # Dashboard, /wifi, /ota, /api/*
    │   └── ota.rs           # OTA upload handler
    └── config/
        └── mod.rs           # NVS wrapper
```

## S21 Protocol Modeling

```rust
enum S21Command {
    SetState(PowerModeTemp),    // D1: power + mode + temp + fan
    SetSwing(SwingAxis, bool),  // D5: vertical/horizontal
    QueryStatus,                // F1 -> G1
    QueryTemp,                  // RH -> SH
    QuerySwing,                 // F5 -> G5
    Extended(FxCommand),        // FX (v3+)
}

enum S21Version {
    V1,
    V2,
    V3 { major: u8, minor: u8 },
    V4,
}

struct S21Frame {
    cmd: [u8; 2],
    payload: heapless::Vec<u8, 16>,
}
```

Version detection state machine: FY00 (v3+) -> FU (v3 variant) -> F1 (v2) -> minimal v1.

## Hardware Abstraction

```rust
#[cfg(feature = "esp32c3")]
const UART_PINS: (i32, i32) = (4, 3);   // RX, TX

#[cfg(feature = "esp32s3")]
const UART_PINS: (i32, i32) = (13, 12); // RX, TX
```

Cargo features: `esp32c3` (default), `esp32s3`.

## Controller Task Design

Carried over from v2 (proven architecture):

- Single task owns all UART I/O (priority 5, 4KB stack)
- Async channel for commands from Matter callbacks (capacity 8)
- Lock-free cached_status for Matter reads (atomic on single-core C3, mutex on dual-core S3)
- Dirty flags: power, mode, temp, fan, swing
- Mode protection: 30s after user set
- Fan protection: 10s after user set
- Poll interval: 6s
- Error recovery: 10 consecutive errors -> 30s recovery interval

## Matter Integration

Using rs-matter crate:

- **Thermostat Cluster**: SystemMode (Off/Heat/Cool/Auto), OccupiedHeatingSetpoint, OccupiedCoolingSetpoint, LocalTemperature
- **Fan Cluster**: FanMode (Auto/Low/Med/High), PercentSetting (0-100)
- **On/Off Cluster**: OnOff (swing toggle)
- **Commissioning**: QR code + numeric pairing code (replaces HomeKit 111-22-333)
- **mDNS**: _matter._tcp service advertisement

## Web Server

Same functional scope as v2, using esp-idf-svc HTTP server:

| Route | Method | Purpose |
|-------|--------|---------|
| `/` | GET | Dashboard (uptime, temp, status, links) |
| `/wifi` | GET | WiFi config form |
| `/wifi-save` | POST | Save credentials, restart |
| `/api/health` | GET | JSON: heap, uptime |
| `/api/metrics` | GET | JSON: temp, RSSI, errors, S21 version |
| `/ota` | GET | OTA upload form |
| `/ota` | POST | Flash firmware |
| `/hotspot-detect.html` | GET | Apple captive portal |
| `/generate_204` | GET | Android captive portal |

AP mode: port 80. STA mode: port 8080.

## WiFi Manager

Same strategy as v2:

1. Load credentials from NVS
2. STA connect with 15 retries (exponential backoff)
3. On failure: APSTA fallback ("DaiSpan-Config", WPA2, password "12345678")
4. DNS captive portal task (all queries -> 192.168.4.1)
5. Conservative TX power (13dBm AP, 8.5dBm STA)

## Toolchain

```
rustup                # Rust stable (ESP32-C3 RISC-V)
espup                 # ESP32-S3 Xtensa toolchain
cargo                 # Build system
espflash              # Flash tool
esp-idf-svc ~0.49     # ESP-IDF Rust bindings
rs-matter              # Pure Rust Matter SDK
heapless               # No-alloc collections
```

## Key Dependencies (Cargo.toml)

```toml
[dependencies]
esp-idf-svc = { version = "0.49", features = ["binstart", "alloc"] }
esp-idf-hal = "0.44"
rs-matter = { git = "https://github.com/project-chip/rs-matter" }
heapless = "0.8"
log = "0.4"
```

## Estimated Size

| Component | LOC estimate |
|-----------|-------------|
| S21 protocol (full) | 600-800 |
| Controller task | 300-400 |
| Matter integration | 300-400 |
| WiFi manager | 200-250 |
| Web server | 300-400 |
| Config (NVS) | 80-100 |
| Main + types | 100-150 |
| **Total** | **~2000-2500** |

Binary: ~1.1-1.3MB estimated (46% of 1.75MB OTA slot).

## Migration from v2

- S21 frame format, timing, commands: direct port (protocol is identical)
- Controller task pattern: same architecture, Rust channel instead of FreeRTOS queue
- WiFi/web: same routes and behavior, different implementation
- HomeKit -> Matter: different commissioning flow, similar cluster concepts
- NVS keys: new namespace to avoid conflicts with v1/v2 data

## Success Criteria

1. Matter device visible in iPhone Home app
2. Thermostat control works (mode, temp, fan, swing)
3. S21 version auto-detection succeeds
4. OTA update works
5. AP mode WiFi config works
6. Builds for both ESP32-C3 and ESP32-S3
