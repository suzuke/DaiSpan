# DaiSpan v3 Rust Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Rewrite DaiSpan as a pure Rust ESP32 firmware using Matter (rs-matter) for smart home integration and full S21 protocol support.

**Architecture:** Single controller task owns UART I/O, Matter reads from cached state and sends commands via async channel. WiFi with AP fallback, web server for config/OTA/metrics.

**Tech Stack:** Rust stable (C3) + espup (S3), esp-idf-svc, rs-matter, heapless, espflash

---

## Prerequisites

Before starting, ensure the development environment is set up:

```bash
# Install Rust ESP32 toolchain
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
cargo install espup espflash ldproxy
espup install  # Installs Xtensa toolchain for ESP32-S3

# Verify
rustup show  # Should show stable + esp toolchain
```

**Reference files (read before each task):**
- v2 S21 protocol: `v2/main/s21/s21_protocol.c` (324 LOC — frame codec, basic commands)
- v2 controller: `v2/main/controller/thermostat_ctrl.c` (475 LOC — task, queue, dirty flags)
- v2 types: `v2/main/common/types.h` (101 LOC — ac_status_t, ctrl_cmd_t, mode conversions)
- v1 S21 version detection: `src/S21Protocol.cpp:76-191` (FY00→FU→F8→F1 state machine)
- v1 FX commands: `include/protocol/S21Protocol.h:36-73` (FX command types)
- v1 features: `include/protocol/IS21Protocol.h:26-68` (S21Features bitfield)

---

### Task 1: Project Scaffold — Cargo + ESP-IDF + Build Verification

**Files:**
- Create: `v3/Cargo.toml`
- Create: `v3/build.rs`
- Create: `v3/sdkconfig.defaults`
- Create: `v3/partitions.csv`
- Create: `v3/src/main.rs`
- Create: `v3/.cargo/config.toml`
- Create: `v3/.gitignore`

**Step 1: Create v3 directory and Cargo.toml**

```bash
mkdir -p v3/src v3/.cargo
```

`v3/Cargo.toml`:
```toml
[package]
name = "daispan"
version = "3.0.0"
edition = "2021"

[dependencies]
esp-idf-svc = { version = "0.49", features = ["binstart"] }
esp-idf-hal = "0.44"
log = "0.4"
heapless = "0.8"

[build-dependencies]
embuild = "0.32"

[profile.release]
opt-level = "s"
lto = "thin"

[profile.dev]
opt-level = "z"
debug = true
```

**Step 2: Create build.rs**

`v3/build.rs`:
```rust
fn main() {
    embuild::espidf::sysenv::output();
}
```

**Step 3: Create .cargo/config.toml for ESP32-C3**

`v3/.cargo/config.toml`:
```toml
[build]
target = "riscv32imc-esp-espidf"

[target.riscv32imc-esp-espidf]
linker = "ldproxy"
runner = "espflash flash --monitor"

[unstable]
build-std = ["std", "panic_abort"]

[env]
MCU = "esp32c3"
ESP_IDF_VERSION = "v5.3"
ESP_IDF_TOOLS_INSTALL_DIR = "global"
```

**Step 4: Create sdkconfig.defaults**

`v3/sdkconfig.defaults`:
```
# DaiSpan v3 ESP-IDF configuration
CONFIG_IDF_TARGET="esp32c3"

# Partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# WiFi
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=12
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=6

# LWIP
CONFIG_LWIP_MAX_SOCKETS=16

# mDNS
CONFIG_MDNS_MAX_SERVICES=5

# Log level
CONFIG_LOG_DEFAULT_LEVEL_INFO=y

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
```

**Step 5: Create partitions.csv**

`v3/partitions.csv`:
```
# Name,    Type, SubType, Offset,   Size,   Flags
nvs,       data, nvs,     0x9000,   0x6000,
otadata,   data, ota,     0xf000,   0x2000,
phy_init,  data, phy,     0x11000,  0x1000,
ota_0,     app,  ota_0,   0x20000,  0x1C0000,
ota_1,     app,  ota_1,   0x1E0000, 0x1C0000,
nvs_keys,  data, nvs_keys,0x3A0000, 0x1000,
```

**Step 6: Create minimal main.rs**

`v3/src/main.rs`:
```rust
use esp_idf_svc::hal::prelude::Peripherals;
use esp_idf_svc::log::EspLogger;
use esp_idf_svc::sys::link_patches;

fn main() {
    link_patches();
    EspLogger::initialize_default();

    log::info!("DaiSpan v3 starting...");

    let peripherals = Peripherals::take().unwrap();
    let _ = peripherals; // Will use later

    log::info!("Free heap: {} bytes", unsafe {
        esp_idf_svc::sys::esp_get_free_heap_size()
    });

    log::info!("DaiSpan v3 initialized (skeleton)");
    loop {
        std::thread::sleep(std::time::Duration::from_secs(5));
        log::info!("heartbeat");
    }
}
```

**Step 7: Create .gitignore**

`v3/.gitignore`:
```
target/
.embuild/
sdkconfig
sdkconfig.old
```

**Step 8: Build and verify**

```bash
cd v3
cargo build --release 2>&1 | tail -5
```

Expected: Successful build, binary in `target/riscv32imc-esp-espidf/release/daispan`

**Step 9: Flash and verify on device**

```bash
espflash flash --monitor target/riscv32imc-esp-espidf/release/daispan -p /dev/cu.usbmodem101
```

Expected: Serial output shows "DaiSpan v3 starting..." and "heartbeat" every 5s.

**Step 10: Commit**

```bash
git add v3/
git commit -m "feat(v3): scaffold Rust ESP-IDF project with build verification"
```

---

### Task 2: S21 Types and Frame Codec

**Files:**
- Create: `v3/src/s21/mod.rs`
- Create: `v3/src/s21/types.rs`
- Create: `v3/src/s21/codec.rs`
- Modify: `v3/src/main.rs` (add `mod s21;`)

**Reference:** `v2/main/common/types.h`, `v2/main/s21/s21_protocol.c:45-159`

**Step 1: Create s21/types.rs with all protocol types**

`v3/src/s21/types.rs`:
```rust
use heapless::Vec;

pub const STX: u8 = 0x02;
pub const ETX: u8 = 0x03;
pub const ACK: u8 = 0x06;
pub const NAK: u8 = 0x15;

pub const ACK_TIMEOUT_MS: u32 = 150;
pub const RESPONSE_TIMEOUT_MS: u32 = 500;
pub const MAX_PAYLOAD_LEN: usize = 16;

/// AC operation modes (S21 internal values)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum AcMode {
    Auto = 0,
    Dry = 1,
    Cool = 2,
    Heat = 3,
    Fan = 6,
    Auto2 = 7,
}

impl AcMode {
    pub fn from_byte(b: u8) -> Self {
        match b {
            0 => Self::Auto,
            1 => Self::Dry,
            2 => Self::Cool,
            3 => Self::Heat,
            6 => Self::Fan,
            7 => Self::Auto2,
            _ => Self::Auto,
        }
    }
}

/// Fan speed levels
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum FanSpeed {
    Auto = 0,
    Quiet = 1,
    Speed1 = 3,
    Speed2 = 4,
    Speed3 = 5,
    Speed4 = 6,
    Speed5 = 7,
}

impl FanSpeed {
    pub fn to_s21_char(self) -> u8 {
        match self {
            Self::Auto => b'A',
            Self::Quiet => b'B',
            Self::Speed1 => b'3',
            Self::Speed2 => b'4',
            Self::Speed3 => b'5',
            Self::Speed4 => b'6',
            Self::Speed5 => b'7',
        }
    }

    pub fn from_s21_char(c: u8) -> Self {
        match c {
            b'A' => Self::Auto,
            b'B' => Self::Quiet,
            b'3' => Self::Speed1,
            b'4' => Self::Speed2,
            b'5' => Self::Speed3,
            b'6' => Self::Speed4,
            b'7' => Self::Speed5,
            _ => Self::Auto,
        }
    }

    /// Convert to 0-100% for Matter fan cluster
    pub fn to_percent(self) -> u8 {
        match self {
            Self::Auto => 0,
            Self::Quiet => 10,
            Self::Speed1 => 20,
            Self::Speed2 => 40,
            Self::Speed3 => 60,
            Self::Speed4 => 80,
            Self::Speed5 => 100,
        }
    }

    /// Convert from 0-100% (Matter fan cluster)
    pub fn from_percent(pct: u8) -> Self {
        match pct {
            0 => Self::Auto,
            1..=15 => Self::Quiet,
            16..=30 => Self::Speed1,
            31..=50 => Self::Speed2,
            51..=70 => Self::Speed3,
            71..=90 => Self::Speed4,
            _ => Self::Speed5,
        }
    }
}

/// Swing axis
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SwingAxis {
    Vertical,
    Horizontal,
}

/// AC status from S21 queries
#[derive(Debug, Clone)]
pub struct AcStatus {
    pub power: bool,
    pub mode: AcMode,
    pub target_temp: f32,
    pub current_temp: f32,
    pub fan_speed: FanSpeed,
    pub swing_vertical: bool,
    pub swing_horizontal: bool,
    pub valid: bool,
}

impl Default for AcStatus {
    fn default() -> Self {
        Self {
            power: false,
            mode: AcMode::Auto,
            target_temp: 21.0,
            current_temp: 21.0,
            fan_speed: FanSpeed::Auto,
            swing_vertical: false,
            swing_horizontal: false,
            valid: false,
        }
    }
}

/// S21 protocol frame
#[derive(Debug, Clone)]
pub struct S21Frame {
    pub cmd: [u8; 2],
    pub payload: Vec<u8, MAX_PAYLOAD_LEN>,
}

/// Errors from S21 communication
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum S21Error {
    Timeout,
    Nak,
    InvalidFrame,
    ChecksumError,
    UartError,
}

/// Temperature encoding: S21 uses `encoded = (temp - 18) * 2 + '@'`
pub fn encode_target_temp(temp: f32) -> u8 {
    let offset = ((temp - 18.0) * 2.0 + 0.5) as i32;
    (b'@' as i32 + offset) as u8
}

pub fn decode_target_temp(encoded: u8) -> f32 {
    18.0 + (encoded as f32 - b'@' as f32) * 0.5
}

/// Room temperature: 3 ASCII digits + sign (e.g., "251+" = 25.1C)
pub fn decode_room_temp(payload: &[u8]) -> Option<f32> {
    if payload.len() < 4 {
        return None;
    }
    for &b in &payload[..3] {
        if !(b'0'..=b'9').contains(&b) {
            return None;
        }
    }
    if payload[3] != b'+' && payload[3] != b'-' {
        return None;
    }
    let val = (payload[0] - b'0') as i32 * 100
        + (payload[1] - b'0') as i32 * 10
        + (payload[2] - b'0') as i32;
    let temp = if payload[3] == b'-' {
        -(val as f32) * 0.1
    } else {
        val as f32 * 0.1
    };
    if temp < -50.0 || temp > 100.0 {
        return None;
    }
    Some(temp)
}
```

**Step 2: Create s21/codec.rs with frame encode/decode**

`v3/src/s21/codec.rs`:
```rust
use super::types::*;

/// Calculate S21 checksum: sum of all bytes between STX and ETX (exclusive)
pub fn checksum(data: &[u8]) -> u8 {
    let mut sum: u8 = 0;
    for &b in data {
        sum = sum.wrapping_add(b);
    }
    sum
}

/// Build a complete S21 frame: STX + cmd + payload + checksum + ETX
pub fn encode_frame(cmd: [u8; 2], payload: &[u8]) -> heapless::Vec<u8, 32> {
    let mut frame = heapless::Vec::new();
    let _ = frame.push(STX);
    let _ = frame.push(cmd[0]);
    let _ = frame.push(cmd[1]);
    for &b in payload {
        let _ = frame.push(b);
    }
    // Checksum covers everything between STX and ETX
    let cksum = checksum(&frame[1..]);
    let _ = frame.push(cksum);
    let _ = frame.push(ETX);
    frame
}

/// Parse a received frame buffer into cmd + payload.
/// Returns None if frame is invalid.
pub fn decode_frame(buf: &[u8]) -> Result<S21Frame, S21Error> {
    // Minimum: STX + cmd0 + cmd1 + checksum + ETX = 5 bytes
    if buf.len() < 5 {
        return Err(S21Error::InvalidFrame);
    }
    if buf[0] != STX || buf[buf.len() - 1] != ETX {
        return Err(S21Error::InvalidFrame);
    }

    // Verify checksum
    let data = &buf[1..buf.len() - 2]; // cmd + payload
    let expected = checksum(data);
    let actual = buf[buf.len() - 2];
    if expected != actual {
        return Err(S21Error::ChecksumError);
    }

    let cmd = [buf[1], buf[2]];
    let payload_bytes = &buf[3..buf.len() - 2];
    let mut payload = heapless::Vec::new();
    for &b in payload_bytes {
        let _ = payload.push(b);
    }

    Ok(S21Frame { cmd, payload })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_encode_decode_roundtrip() {
        let cmd = [b'F', b'1'];
        let payload = [];
        let frame = encode_frame(cmd, &payload);
        let decoded = decode_frame(&frame).unwrap();
        assert_eq!(decoded.cmd, cmd);
        assert!(decoded.payload.is_empty());
    }

    #[test]
    fn test_encode_d1_command() {
        let cmd = [b'D', b'1'];
        let payload = [b'1', b'2', b'F', b'A']; // power=on, mode=cool, temp=21, fan=auto
        let frame = encode_frame(cmd, &payload);
        assert_eq!(frame[0], STX);
        assert_eq!(*frame.last().unwrap(), ETX);
        let decoded = decode_frame(&frame).unwrap();
        assert_eq!(decoded.cmd, [b'D', b'1']);
        assert_eq!(decoded.payload.as_slice(), &payload);
    }

    #[test]
    fn test_bad_checksum() {
        let mut frame = encode_frame([b'F', b'1'], &[]);
        // Corrupt checksum
        let len = frame.len();
        frame[len - 2] = frame[len - 2].wrapping_add(1);
        assert_eq!(decode_frame(&frame), Err(S21Error::ChecksumError));
    }

    #[test]
    fn test_temp_encoding() {
        assert_eq!(decode_target_temp(encode_target_temp(18.0)), 18.0);
        assert_eq!(decode_target_temp(encode_target_temp(21.0)), 21.0);
        assert_eq!(decode_target_temp(encode_target_temp(25.5)), 25.5);
        assert_eq!(decode_target_temp(encode_target_temp(30.0)), 30.0);
    }

    #[test]
    fn test_room_temp_decode() {
        assert_eq!(decode_room_temp(b"251+"), Some(25.1));
        assert_eq!(decode_room_temp(b"100-"), Some(-10.0));
        assert_eq!(decode_room_temp(b"000+"), Some(0.0));
        assert_eq!(decode_room_temp(b"25"), None); // too short
    }

    #[test]
    fn test_fan_speed_roundtrip() {
        for speed in [FanSpeed::Auto, FanSpeed::Quiet, FanSpeed::Speed1,
                      FanSpeed::Speed2, FanSpeed::Speed3, FanSpeed::Speed4, FanSpeed::Speed5] {
            let c = speed.to_s21_char();
            assert_eq!(FanSpeed::from_s21_char(c), speed);
        }
    }
}
```

**Step 3: Create s21/mod.rs**

`v3/src/s21/mod.rs`:
```rust
pub mod types;
pub mod codec;

pub use types::*;
pub use codec::{encode_frame, decode_frame, checksum};
```

**Step 4: Add module to main.rs**

Add `mod s21;` at the top of `v3/src/main.rs`.

**Step 5: Run tests**

```bash
cd v3
cargo test --lib -- --nocapture
```

Expected: All codec and encoding tests pass.

**Step 6: Commit**

```bash
git add v3/src/s21/
git commit -m "feat(v3): add S21 types and frame codec with tests"
```

---

### Task 3: S21 UART Driver

**Files:**
- Create: `v3/src/s21/uart.rs`
- Modify: `v3/src/s21/mod.rs` (add `pub mod uart;`)

**Reference:** `v2/main/s21/s21_protocol.c:10-159` (UART init, send_command, read_response)

**Step 1: Create s21/uart.rs**

`v3/src/s21/uart.rs`:
```rust
use esp_idf_hal::uart::{self, UartDriver, config::Config as UartConfig};
use esp_idf_hal::gpio::AnyIOPin;
use esp_idf_hal::peripheral::Peripheral;
use esp_idf_hal::units::Hertz;
use std::time::{Duration, Instant};

use super::types::*;
use super::codec;

pub struct S21Uart<'d> {
    uart: UartDriver<'d>,
}

impl<'d> S21Uart<'d> {
    pub fn new(
        uart: impl Peripheral<P = impl uart::Uart> + 'd,
        tx: impl Peripheral<P = impl esp_idf_hal::gpio::OutputPin> + 'd,
        rx: impl Peripheral<P = impl esp_idf_hal::gpio::InputPin> + 'd,
    ) -> Result<Self, S21Error> {
        let config = UartConfig::new()
            .baudrate(Hertz(2400))
            .data_bits(uart::config::DataBits::DataBits8)
            .parity(uart::config::Parity::ParityEven)
            .stop_bits(uart::config::StopBits::STOP2)
            .flow_control(uart::config::FlowControl::None);

        let uart = UartDriver::new(
            uart,
            tx,
            rx,
            Option::<AnyIOPin>::None,
            Option::<AnyIOPin>::None,
            &config,
        ).map_err(|_| S21Error::UartError)?;

        log::info!("S21 UART initialized: 2400 baud, 8E2");
        Ok(Self { uart })
    }

    /// Send a command frame and wait for ACK
    pub fn send_command(&self, cmd: [u8; 2], payload: &[u8]) -> Result<(), S21Error> {
        let frame = codec::encode_frame(cmd, payload);

        // Flush RX before sending
        self.uart.clear_rx().map_err(|_| S21Error::UartError)?;

        // Write frame
        self.uart.write(&frame).map_err(|_| S21Error::UartError)?;

        // Wait for ACK
        let mut ack = [0u8; 1];
        let read = self.uart.read(&mut ack, Duration::from_millis(ACK_TIMEOUT_MS as u64))
            .map_err(|_| S21Error::UartError)?;

        if read == 0 {
            log::warn!("ACK timeout for {}{}", cmd[0] as char, cmd[1] as char);
            return Err(S21Error::Timeout);
        }

        match ack[0] {
            ACK => Ok(()),
            NAK => {
                log::warn!("NAK for {}{}", cmd[0] as char, cmd[1] as char);
                Err(S21Error::Nak)
            }
            other => {
                log::warn!("Unexpected 0x{:02X} for {}{}", other, cmd[0] as char, cmd[1] as char);
                Err(S21Error::InvalidFrame)
            }
        }
    }

    /// Read a response frame, send ACK on success
    pub fn read_response(&self) -> Result<S21Frame, S21Error> {
        let mut buf = [0u8; 64];
        let mut total = 0usize;
        let deadline = Instant::now() + Duration::from_millis(RESPONSE_TIMEOUT_MS as u64);

        while total < buf.len() {
            if Instant::now() > deadline {
                log::warn!("Response timeout after {} bytes", total);
                return Err(S21Error::Timeout);
            }

            let remaining = Duration::from_millis(100).min(
                deadline.saturating_duration_since(Instant::now())
            );
            let read = self.uart.read(&mut buf[total..total + 1], remaining)
                .map_err(|_| S21Error::UartError)?;

            if read == 0 {
                continue;
            }
            total += read;
            if buf[total - 1] == ETX {
                break;
            }
        }

        let frame = codec::decode_frame(&buf[..total])?;

        // Send ACK
        self.uart.write(&[ACK]).map_err(|_| S21Error::UartError)?;

        Ok(frame)
    }

    /// Send command + read response (query pattern)
    pub fn query(&self, cmd: [u8; 2], payload: &[u8]) -> Result<S21Frame, S21Error> {
        self.send_command(cmd, payload)?;
        self.read_response()
    }
}
```

**Step 2: Update s21/mod.rs**

```rust
pub mod types;
pub mod codec;
pub mod uart;

pub use types::*;
pub use codec::{encode_frame, decode_frame, checksum};
pub use uart::S21Uart;
```

**Step 3: Build to verify compilation**

```bash
cd v3 && cargo build --release 2>&1 | tail -5
```

Expected: Builds successfully.

**Step 4: Commit**

```bash
git add v3/src/s21/uart.rs v3/src/s21/mod.rs
git commit -m "feat(v3): add S21 UART driver with send/receive/query"
```

---

### Task 4: S21 High-Level Commands

**Files:**
- Create: `v3/src/s21/commands.rs`
- Modify: `v3/src/s21/mod.rs`

**Reference:** `v2/main/s21/s21_protocol.c:216-324` (set_power_and_mode, query_status, query_temperature, query_swing, set_swing)

**Step 1: Create s21/commands.rs**

`v3/src/s21/commands.rs`:
```rust
use super::types::*;
use super::uart::S21Uart;

impl S21Uart<'_> {
    /// D1: Set power, mode, temperature, fan speed
    pub fn set_state(&self, power: bool, mode: AcMode, temp: f32, fan: FanSpeed) -> Result<(), S21Error> {
        let payload = [
            if power { b'1' } else { b'0' },
            b'0' + mode as u8,
            encode_target_temp(temp),
            fan.to_s21_char(),
        ];
        log::info!("D1: power={} mode={:?} temp={:.1} fan={:?}", power, mode, temp, fan);

        match self.send_command([b'D', b'1'], &payload) {
            Ok(()) => Ok(()),
            Err(_) => {
                // Retry once after 50ms
                std::thread::sleep(std::time::Duration::from_millis(50));
                self.send_command([b'D', b'1'], &payload)
            }
        }
    }

    /// F1 -> G1: Query device status
    pub fn query_status(&self) -> Result<(bool, AcMode, f32, FanSpeed), S21Error> {
        let resp = self.query([b'F', b'1'], &[])?;

        if resp.payload.len() < 4 || resp.cmd != [b'G', b'1'] {
            log::warn!("Invalid status response: cmd={}{} len={}",
                resp.cmd[0] as char, resp.cmd[1] as char, resp.payload.len());
            return Err(S21Error::InvalidFrame);
        }

        let power = resp.payload[0] == b'1';
        let mode = AcMode::from_byte(resp.payload[1] - b'0');
        let temp = decode_target_temp(resp.payload[2]);
        let fan = FanSpeed::from_s21_char(resp.payload[3]);

        log::info!("Status: power={} mode={:?} temp={:.1} fan={:?}", power, mode, temp, fan);
        Ok((power, mode, temp, fan))
    }

    /// RH -> SH: Query room temperature
    pub fn query_temperature(&self) -> Result<f32, S21Error> {
        let resp = self.query([b'R', b'H'], &[])?;

        if resp.cmd != [b'S', b'H'] || resp.payload.len() < 4 {
            return Err(S21Error::InvalidFrame);
        }

        decode_room_temp(&resp.payload).ok_or(S21Error::InvalidFrame)
    }

    /// F5 -> G5: Query swing state
    pub fn query_swing(&self) -> Result<(bool, bool), S21Error> {
        let resp = self.query([b'F', b'5'], &[])?;

        if resp.cmd != [b'G', b'5'] || resp.payload.len() < 2 {
            return Err(S21Error::InvalidFrame);
        }

        let vertical = resp.payload[0] != b'0';
        let horizontal = resp.payload[1] != b'0';
        log::debug!("Swing: V={} H={}", vertical, horizontal);
        Ok((vertical, horizontal))
    }

    /// D5: Set swing state
    pub fn set_swing(&self, vertical: bool, horizontal: bool) -> Result<(), S21Error> {
        let payload = [
            if vertical { b'?' } else { b'0' },
            if horizontal { b'?' } else { b'0' },
            b'0',
            b'0',
        ];
        log::info!("D5: V={} H={}", vertical, horizontal);
        self.send_command([b'D', b'5'], &payload)
    }
}
```

**Step 2: Update mod.rs, build, commit**

```bash
cargo build --release
git add v3/src/s21/
git commit -m "feat(v3): add S21 high-level commands (D1, F1, RH, F5, D5)"
```

---

### Task 5: S21 Version Detection

**Files:**
- Create: `v3/src/s21/version.rs`
- Modify: `v3/src/s21/mod.rs`

**Reference:** `src/S21Protocol.cpp:76-191` (FY00→FU→F8→F1 detection chain)

**Step 1: Create s21/version.rs**

`v3/src/s21/version.rs`:
```rust
use super::types::S21Error;
use super::uart::S21Uart;

/// S21 protocol version
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum S21Version {
    V1,
    V2,
    V3 { major: u8, minor: u8 },
    V4,
}

impl std::fmt::Display for S21Version {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::V1 => write!(f, "1.0"),
            Self::V2 => write!(f, "2.0"),
            Self::V3 { major, minor } => write!(f, "{}.{:02}", major, minor),
            Self::V4 => write!(f, "4.0"),
        }
    }
}

impl S21Version {
    pub fn supports_fx(&self) -> bool {
        matches!(self, Self::V3 { major: _, minor } if *minor >= 20)
            || matches!(self, Self::V4)
    }
}

/// FX extended command types (v3.20+)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum FxCommandType {
    Basic = 0x00,
    Sensor = 0x10,
    Control = 0x20,
    Status = 0x30,
    Diagnostic = 0x40,
    Maintenance = 0x50,
    Energy = 0x60,
    System = 0x70,
}

/// Feature flags discovered during initialization
#[derive(Debug, Clone, Default)]
pub struct S21Features {
    pub has_auto_mode: bool,
    pub has_dry_mode: bool,
    pub has_fan_mode: bool,
    pub has_heat_mode: bool,
    pub has_cool_mode: bool,
    pub has_swing: bool,
    pub has_humidity_sensor: bool,
    pub has_outdoor_temp: bool,
    pub has_powerful_mode: bool,
    pub has_eco_mode: bool,
    pub has_quiet_mode: bool,
    pub has_energy_monitoring: bool,
}

impl S21Uart<'_> {
    /// Detect S21 protocol version using progressive probing
    pub fn detect_version(&self) -> S21Version {
        // Step 1: Try FY00 (v3+ version query)
        if let Ok(resp) = self.query([b'F', b'Y'], &[b'0', b'0']) {
            if resp.cmd == [b'G', b'Y'] && resp.payload.len() >= 4 {
                if let Some(ver) = parse_version_string(&resp.payload) {
                    log::info!("S21 version detected: {}", ver);
                    return ver;
                }
            }
        }

        // Step 2: Try FU (some v3 variants)
        if let Ok(resp) = self.query([b'F', b'U'], &[]) {
            if resp.cmd == [b'G', b'U'] {
                log::info!("S21 v3 variant detected (via FU)");
                return S21Version::V3 { major: 3, minor: 0 };
            }
        }

        // Step 3: Try F8 (v2)
        if let Ok(resp) = self.query([b'F', b'8'], &[]) {
            if resp.cmd == [b'G', b'8'] {
                log::info!("S21 v2 detected");
                return S21Version::V2;
            }
        }

        // Step 4: Try F1 (v1)
        if self.query([b'F', b'1'], &[]).is_ok() {
            log::info!("S21 v1 detected");
            return S21Version::V1;
        }

        log::warn!("S21 version detection failed, assuming V1");
        S21Version::V1
    }

    /// Detect supported features based on version and probing
    pub fn detect_features(&self, version: &S21Version) -> S21Features {
        let mut features = S21Features::default();

        // All versions support basic modes
        if *version >= S21Version::V1 {
            features.has_cool_mode = true;
            features.has_heat_mode = true;
            features.has_auto_mode = true;
        }

        if *version >= S21Version::V2 {
            features.has_dry_mode = true;
            features.has_fan_mode = true;
        }

        // Probe swing support
        if self.query([b'F', b'5'], &[]).is_ok() {
            features.has_swing = true;
        }

        // v3+ features
        if *version >= (S21Version::V3 { major: 3, minor: 0 }) {
            features.has_powerful_mode = true;
            features.has_eco_mode = true;
            features.has_quiet_mode = true;
        }

        if *version >= (S21Version::V3 { major: 3, minor: 20 }) {
            features.has_energy_monitoring = true;
        }

        log::info!("S21 features: {:?}", features);
        features
    }

    /// Probe FX extended commands (v3.20+)
    pub fn probe_fx_commands(&self) -> heapless::Vec<FxCommandType, 8> {
        let mut supported = heapless::Vec::new();
        let fx_types = [
            FxCommandType::Basic,
            FxCommandType::Sensor,
            FxCommandType::Control,
            FxCommandType::Status,
            FxCommandType::Diagnostic,
            FxCommandType::Maintenance,
            FxCommandType::Energy,
            FxCommandType::System,
        ];

        for &fx in &fx_types {
            let payload = [fx as u8, 0x00];
            if let Ok(resp) = self.query([b'F', b'X'], &payload) {
                if resp.cmd == [b'G', b'X'] {
                    let _ = supported.push(fx);
                    log::debug!("FX {:02X} supported", fx as u8);
                }
            }
        }

        log::info!("FX commands supported: {}/{}", supported.len(), fx_types.len());
        supported
    }
}

fn parse_version_string(data: &[u8]) -> Option<S21Version> {
    // Format: "3.xx" or "4.xx"
    if data.len() < 4 || data[1] != b'.' {
        return None;
    }
    let major = (data[0] as char).to_digit(10)? as u8;
    let minor_tens = (data[2] as char).to_digit(10)? as u8;
    let minor_ones = if data.len() >= 4 && data[3].is_ascii_digit() {
        (data[3] as char).to_digit(10).unwrap_or(0) as u8
    } else {
        0
    };
    let minor = minor_tens * 10 + minor_ones;

    match major {
        3 => Some(S21Version::V3 { major: 3, minor }),
        4.. => Some(S21Version::V4),
        _ => None,
    }
}
```

**Step 2: Update mod.rs, build, commit**

```bash
cargo build --release
git add v3/src/s21/
git commit -m "feat(v3): add S21 version detection and FX command probing"
```

---

### Task 6: Controller Task with Channel + Cached State

**Files:**
- Create: `v3/src/controller/mod.rs`
- Create: `v3/src/controller/state.rs`
- Create: `v3/src/controller/task.rs`
- Modify: `v3/src/main.rs`

**Reference:** `v2/main/controller/thermostat_ctrl.c` (entire file — 475 LOC)

**Step 1: Create controller/state.rs**

Contains `AcStatus` cached state and command types. Use `std::sync::Mutex` for dual-core safety (ESP32-S3), which degrades to a simple lock on single-core ESP32-C3.

`v3/src/controller/state.rs`:
```rust
use std::sync::{Arc, Mutex};
use crate::s21::types::*;

/// Command sent from Matter/Web to the controller task
#[derive(Debug, Clone)]
pub enum ControllerCmd {
    SetPower(bool),
    SetMode(AcMode),
    SetTemp(f32),
    SetFan(FanSpeed),
    SetSwing(SwingAxis, bool),
}

/// Shared cached state, readable by Matter and web server
#[derive(Clone)]
pub struct SharedState {
    inner: Arc<Mutex<AcStatus>>,
}

impl SharedState {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(Mutex::new(AcStatus::default())),
        }
    }

    pub fn read(&self) -> AcStatus {
        self.inner.lock().unwrap().clone()
    }

    pub fn update(&self, status: &AcStatus) {
        *self.inner.lock().unwrap() = status.clone();
    }
}
```

**Step 2: Create controller/task.rs**

Port the v2 controller task loop: command processing, polling, dirty flags, error recovery, mode/fan protection.

`v3/src/controller/task.rs`:
```rust
use std::sync::mpsc;
use std::time::{Duration, Instant};

use crate::s21::types::*;
use crate::s21::uart::S21Uart;
use crate::s21::version::{S21Version, S21Features};
use super::state::*;

const POLL_INTERVAL: Duration = Duration::from_secs(6);
const ERROR_RECOVERY: Duration = Duration::from_secs(30);
const MODE_PROTECT: Duration = Duration::from_secs(30);
const FAN_PROTECT: Duration = Duration::from_secs(10);
const MAX_ERRORS: u32 = 10;
const CMD_QUEUE_SIZE: usize = 8;

/// Matter mode values (matches Matter Thermostat SystemMode)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum MatterMode {
    Off = 0,
    Heat = 4,
    Cool = 3,
    Auto = 1,
}

impl MatterMode {
    pub fn to_ac_mode(self) -> AcMode {
        match self {
            Self::Heat => AcMode::Heat,
            Self::Cool => AcMode::Cool,
            Self::Auto | Self::Off => AcMode::Auto,
        }
    }

    pub fn from_ac_mode(mode: AcMode) -> Self {
        match mode {
            AcMode::Heat => Self::Heat,
            AcMode::Cool => Self::Cool,
            AcMode::Auto | AcMode::Auto2 => Self::Auto,
            _ => Self::Auto, // DRY/FAN -> Auto
        }
    }
}

pub struct Controller<'d> {
    uart: S21Uart<'d>,
    rx: mpsc::Receiver<ControllerCmd>,
    state: SharedState,
    local: AcStatus,
    // Dirty flags
    dirty_power: bool,
    dirty_mode: bool,
    dirty_temp: bool,
    dirty_fan: bool,
    dirty_swing: bool,
    // Error tracking
    consecutive_errors: u32,
    last_success: Instant,
    // User interaction protection
    last_mode_set: Option<Instant>,
    last_user_mode: AcMode,
    last_fan_set: Option<Instant>,
    last_user_fan: FanSpeed,
    // Preserve Matter mode across lossy conversions
    target_matter_mode: MatterMode,
    // Protocol info
    pub version: S21Version,
    pub features: S21Features,
}

impl<'d> Controller<'d> {
    pub fn new(
        uart: S21Uart<'d>,
        state: SharedState,
    ) -> (Self, mpsc::SyncSender<ControllerCmd>) {
        let (tx, rx) = mpsc::sync_channel(CMD_QUEUE_SIZE);
        let ctrl = Self {
            uart,
            rx,
            state,
            local: AcStatus::default(),
            dirty_power: false,
            dirty_mode: false,
            dirty_temp: false,
            dirty_fan: false,
            dirty_swing: false,
            consecutive_errors: 0,
            last_success: Instant::now(),
            last_mode_set: None,
            last_user_mode: AcMode::Auto,
            last_fan_set: None,
            last_user_fan: FanSpeed::Auto,
            target_matter_mode: MatterMode::Auto,
            version: S21Version::V1,
            features: S21Features::default(),
        };
        (ctrl, tx)
    }

    /// Initialize: detect version and features
    pub fn init(&mut self) {
        self.version = self.uart.detect_version();
        self.features = self.uart.detect_features(&self.version);
        log::info!("Controller initialized: S21 {}", self.version);
    }

    fn is_recovering(&self) -> bool {
        self.consecutive_errors >= MAX_ERRORS
    }

    fn handle_error(&mut self, op: &str) {
        self.consecutive_errors += 1;
        log::warn!("{} failed (errors: {})", op, self.consecutive_errors);
        if self.consecutive_errors >= MAX_ERRORS {
            log::error!("Entering error recovery mode");
        }
    }

    fn reset_errors(&mut self) {
        if self.consecutive_errors > 0 {
            log::info!("Errors cleared (was: {})", self.consecutive_errors);
            self.consecutive_errors = 0;
        }
        self.last_success = Instant::now();
    }

    fn try_send_d1(&mut self) -> bool {
        match self.uart.set_state(
            self.local.power, self.local.mode,
            self.local.target_temp, self.local.fan_speed,
        ) {
            Ok(()) => {
                self.dirty_power = false;
                self.dirty_mode = false;
                self.dirty_temp = false;
                self.dirty_fan = false;
                self.reset_errors();
                true
            }
            Err(_) => {
                self.handle_error("send_d1");
                false
            }
        }
    }

    fn sync_dirty(&mut self) {
        if !self.dirty_power && !self.dirty_mode && !self.dirty_temp
            && !self.dirty_fan && !self.dirty_swing
        {
            return;
        }

        if self.dirty_power || self.dirty_mode || self.dirty_fan || self.dirty_temp {
            self.try_send_d1();
        }

        if self.dirty_swing {
            match self.uart.set_swing(self.local.swing_vertical, self.local.swing_horizontal) {
                Ok(()) => {
                    self.dirty_swing = false;
                    self.reset_errors();
                }
                Err(_) => self.handle_error("sync_swing"),
            }
        }
    }

    fn process_command(&mut self, cmd: ControllerCmd) {
        match cmd {
            ControllerCmd::SetPower(on) => {
                log::info!("Set power: {}", on);
                self.local.power = on;
                self.dirty_power = true;
                if !self.is_recovering() { self.try_send_d1(); }
            }
            ControllerCmd::SetMode(mode) => {
                log::info!("Set mode: {:?}", mode);
                self.local.mode = mode;
                self.target_matter_mode = MatterMode::from_ac_mode(mode);
                self.last_mode_set = Some(Instant::now());
                self.last_user_mode = mode;
                self.dirty_mode = true;
                if !self.local.power {
                    self.local.power = true;
                    self.dirty_power = true;
                }
                if !self.is_recovering() { self.try_send_d1(); }
            }
            ControllerCmd::SetTemp(temp) => {
                log::info!("Set temp: {:.1}", temp);
                self.local.target_temp = temp.clamp(16.0, 30.0);
                self.dirty_temp = true;
                if !self.is_recovering() { self.try_send_d1(); }
            }
            ControllerCmd::SetFan(fan) => {
                log::info!("Set fan: {:?}", fan);
                self.local.fan_speed = fan;
                self.last_fan_set = Some(Instant::now());
                self.last_user_fan = fan;
                self.dirty_fan = true;
                if !self.is_recovering() { self.try_send_d1(); }
            }
            ControllerCmd::SetSwing(axis, enabled) => {
                log::info!("Set swing: {:?}={}", axis, enabled);
                match axis {
                    SwingAxis::Vertical => self.local.swing_vertical = enabled,
                    SwingAxis::Horizontal => self.local.swing_horizontal = enabled,
                }
                self.dirty_swing = true;
                if !self.is_recovering() {
                    if let Err(_) = self.uart.set_swing(
                        self.local.swing_vertical, self.local.swing_horizontal
                    ) {
                        self.handle_error("set_swing");
                    } else {
                        self.dirty_swing = false;
                        self.reset_errors();
                    }
                }
            }
        }
        self.state.update(&self.local);
    }

    fn poll_status(&mut self) {
        let now = Instant::now();
        let mut any_success = false;

        // Query F1 -> G1
        match self.uart.query_status() {
            Ok((power, mode, temp, fan)) => {
                any_success = true;
                self.local.power = power;

                // Mode protection
                let mode_protected = self.last_mode_set
                    .map_or(false, |t| now.duration_since(t) < MODE_PROTECT);
                if mode_protected {
                    self.local.mode = self.last_user_mode;
                } else {
                    self.local.mode = mode;
                    match mode {
                        AcMode::Heat => self.target_matter_mode = MatterMode::Heat,
                        AcMode::Cool => self.target_matter_mode = MatterMode::Cool,
                        AcMode::Auto | AcMode::Auto2 => self.target_matter_mode = MatterMode::Auto,
                        _ => {} // DRY/FAN: preserve target_matter_mode
                    }
                }

                self.local.target_temp = temp;

                // Fan protection
                let fan_protected = self.last_fan_set
                    .map_or(false, |t| now.duration_since(t) < FAN_PROTECT);
                if fan_protected {
                    self.local.fan_speed = self.last_user_fan;
                } else {
                    self.local.fan_speed = fan;
                }
            }
            Err(_) => self.handle_error("poll_status"),
        }

        // Query RH -> SH
        if any_success {
            match self.uart.query_temperature() {
                Ok(temp) => self.local.current_temp = temp,
                Err(_) => {
                    self.handle_error("poll_temp");
                    any_success = false;
                }
            }
        }

        // Query F5 -> G5 (optional, don't count as error)
        if any_success {
            if let Ok((sv, sh)) = self.uart.query_swing() {
                self.local.swing_vertical = sv;
                self.local.swing_horizontal = sh;
            }
        }

        if any_success {
            self.local.valid = true;
            self.reset_errors();
        }

        self.state.update(&self.local);
    }

    /// Main controller loop — runs forever on a dedicated thread
    pub fn run(&mut self) {
        log::info!("Controller task started");
        std::thread::sleep(Duration::from_secs(1));
        self.poll_status();

        let mut last_poll = Instant::now();

        loop {
            // Drain command queue (non-blocking, 100ms timeout per attempt)
            while let Ok(cmd) = self.rx.recv_timeout(Duration::from_millis(100)) {
                self.process_command(cmd);
            }

            let now = Instant::now();

            if self.is_recovering() {
                if now.duration_since(self.last_success) > ERROR_RECOVERY {
                    log::info!("Attempting recovery...");
                    self.last_success = now;
                    self.consecutive_errors = MAX_ERRORS - 1;
                    self.sync_dirty();
                    if !self.is_recovering() {
                        self.poll_status();
                        last_poll = Instant::now();
                    }
                }
            } else if now.duration_since(last_poll) >= POLL_INTERVAL {
                self.sync_dirty();
                if !self.is_recovering() {
                    self.poll_status();
                }
                last_poll = Instant::now();
            }
        }
    }

    /// Get current Matter mode (for read callbacks)
    pub fn get_matter_mode(&self) -> MatterMode {
        if !self.local.power {
            MatterMode::Off
        } else {
            self.target_matter_mode
        }
    }
}
```

**Step 3: Create controller/mod.rs**

```rust
pub mod state;
pub mod task;

pub use state::*;
pub use task::*;
```

**Step 4: Update main.rs to spawn controller thread**

```rust
mod s21;
mod controller;

use esp_idf_svc::hal::prelude::Peripherals;
use esp_idf_svc::hal::uart;
use esp_idf_svc::log::EspLogger;
use esp_idf_svc::sys::link_patches;

use crate::s21::S21Uart;
use crate::controller::{SharedState, Controller};

fn main() {
    link_patches();
    EspLogger::initialize_default();
    log::info!("DaiSpan v3 starting...");

    let peripherals = Peripherals::take().unwrap();

    log::info!("Free heap: {} bytes", unsafe {
        esp_idf_svc::sys::esp_get_free_heap_size()
    });

    // S21 UART
    let uart = S21Uart::new(
        peripherals.uart1,
        peripherals.pins.gpio3, // TX
        peripherals.pins.gpio4, // RX
    ).expect("S21 UART init failed");

    // Shared state
    let state = SharedState::new();
    let (mut ctrl, _cmd_tx) = Controller::new(uart, state.clone());

    // Controller thread
    std::thread::Builder::new()
        .name("ctrl".into())
        .stack_size(4096)
        .spawn(move || {
            ctrl.init();
            ctrl.run();
        })
        .expect("Failed to spawn controller thread");

    log::info!("System ready. Free heap: {} bytes", unsafe {
        esp_idf_svc::sys::esp_get_free_heap_size()
    });

    // Main loop placeholder (Matter + web will go here)
    loop {
        std::thread::sleep(std::time::Duration::from_secs(5));
    }
}
```

**Step 5: Build, flash, verify S21 timeout messages**

```bash
cd v3
cargo build --release
espflash flash --monitor target/riscv32imc-esp-espidf/release/daispan -p /dev/cu.usbmodem101
```

Expected: "Controller task started", then S21 ACK timeouts (no AC connected).

**Step 6: Commit**

```bash
git add v3/src/controller/ v3/src/main.rs
git commit -m "feat(v3): add controller task with command queue and cached state"
```

---

### Task 7: Config Manager (NVS)

**Files:**
- Create: `v3/src/config/mod.rs`
- Modify: `v3/src/main.rs`

**Reference:** `v2/main/config/config_manager.c` (95 LOC)

**Step 1: Create config/mod.rs**

```rust
use esp_idf_svc::nvs::{EspNvs, EspNvsPartition, NvsDefault};
use esp_idf_svc::nvs_storage::EspNvsStorage;

const NVS_NAMESPACE: &str = "daispan3";

pub struct Config {
    nvs: EspNvs<NvsDefault>,
}

impl Config {
    pub fn new(partition: EspNvsPartition<NvsDefault>) -> Result<Self, esp_idf_svc::sys::EspError> {
        let nvs = EspNvs::new(partition, NVS_NAMESPACE, true)?;
        log::info!("NVS initialized (namespace: {})", NVS_NAMESPACE);
        Ok(Self { nvs })
    }

    pub fn get_wifi_ssid(&self) -> Option<String> {
        let mut buf = [0u8; 33];
        self.nvs.get_str("wifi_ssid", &mut buf).ok().flatten().map(|s| s.to_string())
    }

    pub fn get_wifi_password(&self) -> Option<String> {
        let mut buf = [0u8; 65];
        self.nvs.get_str("wifi_pass", &mut buf).ok().flatten().map(|s| s.to_string())
    }

    pub fn set_wifi_credentials(&mut self, ssid: &str, password: &str) -> Result<(), esp_idf_svc::sys::EspError> {
        self.nvs.set_str("wifi_ssid", ssid)?;
        self.nvs.set_str("wifi_pass", password)?;
        log::info!("WiFi credentials saved: SSID='{}'", ssid);
        Ok(())
    }
}
```

**Step 2: Build, commit**

```bash
cargo build --release
git add v3/src/config/
git commit -m "feat(v3): add NVS config manager for WiFi credentials"
```

---

### Task 8: WiFi Manager (STA + AP Fallback)

**Files:**
- Create: `v3/src/wifi/mod.rs`
- Create: `v3/src/wifi/ap.rs`
- Modify: `v3/src/main.rs`

**Reference:** `v2/main/wifi/wifi_manager.c` (245 LOC — STA, AP, DNS captive portal)

This is a large task. Implement STA connection with retry, AP fallback with DNS captive portal. Use `esp_idf_svc::wifi::EspWifi` and `esp_idf_svc::wifi::BlockingWifi`.

**Step 1: Create wifi/mod.rs** — STA connect + AP fallback logic
**Step 2: Create wifi/ap.rs** — DNS captive portal task (UDP socket responding to all queries with 192.168.4.1)
**Step 3: Integrate into main.rs** — WiFi init before controller
**Step 4: Flash, test AP mode** — verify phone can connect to DaiSpan-Config
**Step 5: Test STA mode** — configure WiFi via AP, verify STA connect after reboot
**Step 6: Commit**

```bash
git commit -m "feat(v3): add WiFi manager with STA/AP fallback and DNS captive portal"
```

---

### Task 9: Web Server (Dashboard + WiFi Config + OTA + API)

**Files:**
- Create: `v3/src/web/mod.rs`
- Create: `v3/src/web/routes.rs`
- Create: `v3/src/web/ota.rs`
- Modify: `v3/src/main.rs`

**Reference:** `v2/main/web/web_server.c` (383 LOC — all routes)

Routes to implement:
- `GET /` — dashboard HTML
- `GET /wifi` — WiFi config form
- `POST /wifi-save` — save credentials + restart (with URL decode)
- `GET /api/health` — JSON health
- `GET /api/metrics` — JSON metrics
- `GET /ota` — OTA form
- `POST /ota` — OTA upload
- `GET /hotspot-detect.html` — Apple captive portal
- `GET /generate_204` — Android captive portal

**Step 1-5: Implement each route group, test individually**
**Step 6: Commit**

```bash
git commit -m "feat(v3): add web server with dashboard, WiFi config, OTA, and API endpoints"
```

---

### Task 10: Matter Integration

**Files:**
- Create: `v3/src/matter/mod.rs`
- Create: `v3/src/matter/thermostat.rs`
- Create: `v3/src/matter/fan.rs`
- Create: `v3/src/matter/swing.rs`
- Modify: `v3/Cargo.toml` (add rs-matter dependency)
- Modify: `v3/src/main.rs`

**Reference:** rs-matter examples at https://github.com/project-chip/rs-matter/tree/main/examples

This is the most complex task. Steps:

**Step 1: Add rs-matter to Cargo.toml**
**Step 2: Create Matter device with 3 endpoints:**
- Endpoint 1: Thermostat cluster (SystemMode, LocalTemperature, OccupiedHeatingSetpoint, OccupiedCoolingSetpoint)
- Endpoint 2: Fan cluster (FanMode, PercentSetting)
- Endpoint 3: On/Off cluster (swing)
**Step 3: Wire read callbacks to SharedState**
**Step 4: Wire write callbacks to cmd_tx channel**
**Step 5: Test with iPhone Home app — verify device appears and responds**
**Step 6: Commit**

```bash
git commit -m "feat(v3): add Matter integration with Thermostat, Fan, and Swing clusters"
```

---

### Task 11: ESP32-S3 Support

**Files:**
- Modify: `v3/Cargo.toml` (add features)
- Create: `v3/.cargo/config-s3.toml` (Xtensa target)
- Modify: `v3/src/main.rs` (cfg-gated pin selection)

**Step 1: Add feature flags to Cargo.toml**

```toml
[features]
default = ["esp32c3"]
esp32c3 = []
esp32s3 = []
```

**Step 2: Add conditional pin mapping in main.rs**

```rust
#[cfg(feature = "esp32c3")]
let (tx_pin, rx_pin) = (peripherals.pins.gpio3, peripherals.pins.gpio4);

#[cfg(feature = "esp32s3")]
let (tx_pin, rx_pin) = (peripherals.pins.gpio12, peripherals.pins.gpio13);
```

**Step 3: Create S3 cargo config, build for S3, verify**
**Step 4: Commit**

```bash
git commit -m "feat(v3): add ESP32-S3 support via cargo features"
```

---

### Task 12: Integration Test on Hardware

**Files:**
- Modify: various (bug fixes from testing)

**Step 1: Flash to ESP32-C3, verify full boot sequence**
**Step 2: Test AP mode WiFi config from iPhone**
**Step 3: Test STA reconnect after reboot**
**Step 4: Test Matter discovery in iPhone Home app**
**Step 5: Test thermostat control (if AC available)**
**Step 6: Test OTA upload**
**Step 7: Fix any bugs found**
**Step 8: Final commit**

```bash
git commit -m "fix(v3): integration test fixes"
```

---

## Task Dependency Graph

```
T1 (scaffold) -> T2 (types/codec) -> T3 (UART) -> T4 (commands) -> T5 (version)
                                                                         |
T7 (config) -> T8 (WiFi) -> T9 (web)                                    |
                                  \                                      v
                                   +-----------> T10 (Matter) <--- T6 (controller)
                                                      |
                                                      v
                                                 T11 (S3 support)
                                                      |
                                                      v
                                                 T12 (integration test)
```

**Parallel tracks:**
- Track A: T1 → T2 → T3 → T4 → T5 → T6 (S21 + controller)
- Track B: T7 → T8 → T9 (platform services)
- Merge: T10 (Matter needs both tracks)
- Final: T11 → T12
