# DaiSpan - Daikin S21 HomeKit Bridge

A HomeKit bridge for Daikin air conditioners with S21 port, based on ESP32 and HomeSpan.

[繁體中文版](README_TW.md)

This project is based on:
- [HomeSpan](https://github.com/HomeSpan/HomeSpan) - HomeKit library for ESP32
- [ESP32-Faikin](https://github.com/revk/ESP32-Faikin) - S21 protocol implementation

## Copyright Notice

Copyright (c) 2024 DaiSpan

This project incorporates code and concepts from:
- HomeSpan (Copyright © 2020-2024 Gregg E. Berman)
- ESP32-Faikin (Copyright © 2022 Adrian Kennard)

Licensed under the MIT License. This means:
- You can freely use, modify, and distribute this software
- You must include the original copyright notice and license
- The software is provided "as is", without warranty of any kind

## Features

- Support for Daikin S21 protocol versions 1.0, 2.0, and 3.xx
- Auto protocol version detection
- Temperature monitoring and control
- Operation mode control (Cool/Heat/Auto/Fan/Dry)
- Fan speed control
- Power on/off control
- Real-time status updates
- Web-based logging interface
- OTA firmware updates

## Hardware Requirements

- ESP32-S3-DevKitC-1 (recommended)
- TTL to S21 adapter (3.3V level)
- Daikin air conditioner with S21 port

## Pin Configuration

- S21 RX: GPIO16 (RX2)
- S21 TX: GPIO17 (TX2)
- Status LED: GPIO2

## Installation

1. Install PlatformIO
2. Clone this repository
3. Configure WiFi credentials in `src/main.cpp`
4. Build and upload to ESP32

## Usage

1. Power on the device
2. Wait for WiFi connection (Status LED will indicate)
3. Add accessory in Home app using the pairing code
4. Default pairing code: 11122333

## Protocol Support

- Basic commands (all versions):
  - Status query/set (F1/D1)
  - Temperature query (RH)
- Version 2.0+:
  - Extended status (F8/D8)
  - Auto mode
  - Dehumidify mode
  - Fan mode
- Version 3.xx:
  - Feature detection (F2)
  - Version query (FY)
  - Additional settings (FK/FU)

## Development

- Built with PlatformIO
- Uses HomeSpan library for HomeKit integration
- Modular architecture for easy extension

## License

MIT License

## Acknowledgments

- HomeSpan project
- Daikin S21 protocol documentation 