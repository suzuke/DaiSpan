#pragma once

#include "IS21Protocol.h"
#include "../common/Debug.h"
#include "../common/ThermostatMode.h"

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

class S21Protocol : public IS21Protocol {
private:
    HardwareSerial& serial;
    S21ProtocolVersion protocolVersion;
    S21Features features;
    S21Status status;  // 新增狀態追蹤
    bool isInitialized;

    // 內部方法
    bool detectProtocolVersion();
    bool detectFeatures();
    bool sendCommandInternal(char cmd0, char cmd1, const uint8_t* payload = nullptr, size_t len = 0);
    bool waitForAck(unsigned long timeout = 200);
    
    // 錯誤處理內部方法
    void setError(S21ErrorCode error);
    void incrementSuccess();
    void incrementError();
    bool validateResponse(const uint8_t* buffer, size_t len);
    bool validatePayload(const uint8_t* payload, size_t len);

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
    
    // 錯誤處理和狀態檢測實現
    const S21Status& getStatus() const override { return status; }
    S21ErrorCode getLastError() const override { return status.lastError; }
    void clearErrors() override;
    bool hasErrors() const override { return status.hasErrors; }
    uint32_t getErrorCount() const override { return status.communicationErrors; }
    float getSuccessRate() const override;
}; 