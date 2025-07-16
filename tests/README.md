# DaiSpan Test Suite

This directory contains test scripts for the DaiSpan smart thermostat project.

## Test Files

### HomeKit Tests
- `test_homekit_events.py` - Tests HomeKit event handling
- `test_homekit_v3.py` - Tests HomeKit V3 architecture integration

### Real Hardware Tests
- `test_real_ac_v3.py` - Comprehensive real AC testing with V3 architecture
- `test_real_simple.py` - Simple real hardware tests

### V3 Architecture Tests
- `test_v3_event_manual.py` - Manual V3 event system testing
- `trigger_v3_events.py` - V3 event trigger utility

## Usage

Make sure the ESP32 device is connected and accessible before running tests:

```bash
# Install required dependencies
pip install requests websockets

# Run a specific test
python3 test_homekit_v3.py

# Run real hardware tests (requires AC connection)
python3 test_real_ac_v3.py
```

## Test Environment Requirements

- ESP32 device with DaiSpan firmware
- Network connectivity to device
- Python 3.6+ with requests and websockets libraries
- For real hardware tests: S21-compatible Daikin AC unit

## Note

These tests are designed to work with the DaiSpan V3 event-driven architecture. Make sure your device is running the latest firmware with V3 support enabled.