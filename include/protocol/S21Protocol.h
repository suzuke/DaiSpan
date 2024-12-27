#pragma once

#include "IS21Protocol.h"
#include "../common/Debug.h"

// Faikin mode enums
#define FAIKIN_MODE_FAN     0
#define FAIKIN_MODE_HEAT    1
#define FAIKIN_MODE_COOL    2
#define FAIKIN_MODE_AUTO    3
// Values 4, 5, 6 are not used !!!
#define FAIKIN_MODE_DRY     7
#define FAIKIN_MODE_INVALID -1

// Faikin fan speed enums
#define FAIKIN_FAN_AUTO    0
#define FAIKIN_FAN_1       1
#define FAIKIN_FAN_2       2
#define FAIKIN_FAN_3       3
#define FAIKIN_FAN_4       4
#define FAIKIN_FAN_5       5
#define FAIKIN_FAN_QUIET   6
#define FAIKIN_FAN_INVALID -1

// Some common S21 definitions
#define	STX	2
#define	ETX	3
#define	ACK	6
#define	NAK	21

// Packet structure
#define S21_STX_OFFSET     0
#define S21_CMD0_OFFSET    1
#define S21_CMD1_OFFSET    2
#define S21_PAYLOAD_OFFSET 3

// Length of a framing (STX + CRC + ETX)
#define S21_FRAMING_LEN 3
// A typical payload length, but there are deviations
#define S21_PAYLOAD_LEN 4
// A minimum length of a packet (with no payload): framing + CMD0 + CMD1
#define S21_MIN_PKT_LEN (S21_FRAMING_LEN + 2)

// v3 packets use 4-character command codes
#define S21_V3_CMD2_OFFSET 3
#define S21_V3_CMD3_OFFSET 4
#define S21_V3_PAYLOAD_OFFSET 5
#define S21_MIN_V3_PKT_LEN (S21_FRAMING_LEN + 4)

// Encoding for minimum target temperature value, correspond to 18 deg.C.
#define AC_MIN_TEMP_VALUE '@'

// 空調模式定義
#define AC_MODE_DRY  2
#define AC_MODE_COOL 3
#define AC_MODE_HEAT 4
#define AC_MODE_FAN  6
#define AC_MODE_AUTO 7

// Fan speed
#define AC_FAN_AUTO  'A'
#define AC_FAN_QUIET 'B'
#define AC_FAN_1     '3'
#define AC_FAN_2     '4'
#define AC_FAN_3     '5'
#define AC_FAN_4     '6'
#define AC_FAN_5     '7'

// Utility functions
static inline uint8_t s21_checksum(uint8_t* buf, int len);
static inline float s21_decode_target_temp(unsigned char v);
static inline float s21_encode_target_temp(float temp);
static inline int s21_decode_int_sensor(const unsigned char* payload);
static inline uint16_t s21_decode_hex_sensor(const unsigned char* payload);
static inline float s21_decode_float_sensor(const unsigned char* payload);
static inline unsigned char s21_encode_fan(int speed);
static inline int s21_decode_fan(unsigned char v);

class S21Protocol : public IS21Protocol {
private:
    HardwareSerial& serial;
    S21ProtocolVersion protocolVersion;
    S21Features features;
    bool isInitialized;

    // 內部方法
    bool detectProtocolVersion();
    bool detectFeatures();
    bool sendCommandInternal(char cmd0, char cmd1, const uint8_t* payload = nullptr, size_t len = 0);
    bool waitForAck(unsigned long timeout = 100);
    uint8_t mapDaikinMode(uint8_t daikinMode);
    const char* getDaikinModeText(uint8_t daikinMode);

public:
    explicit S21Protocol(HardwareSerial& s);
    
    // IS21Protocol interface implementation
    bool begin() override;
    bool sendCommand(char cmd0, char cmd1, const uint8_t* payload = nullptr, size_t len = 0) override;
    bool parseResponse(uint8_t& cmd0, uint8_t& cmd1, uint8_t* payload, size_t& payloadLen) override;
    S21ProtocolVersion getProtocolVersion() const override { return protocolVersion; }
    const S21Features& getFeatures() const override { return features; }
    bool isFeatureSupported(const S21Features& feature) const override;
    bool isCommandSupported(char cmd0, char cmd1) const override;
}; 