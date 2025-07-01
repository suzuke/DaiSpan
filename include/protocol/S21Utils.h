#pragma once

#include "../common/ThermostatMode.h"

// S21 協議常量定義
#ifndef STX
#define STX 2
#endif
#ifndef ETX
#define ETX 3
#endif
#ifndef ACK
#define ACK 6
#endif
#ifndef AC_MIN_TEMP_VALUE
#define AC_MIN_TEMP_VALUE '@'
#endif

// Calculate packet checksum
static inline uint8_t s21_checksum(uint8_t* buf, int len) {
    uint8_t c = 0;

    // The checksum excludes STX, checksum field itself, and ETX
    for (int i = 1; i < len - 2; i++)
        c += buf[i];

    // Special bytes are forbidden even as checksum bytes, they are promoted
    if (c == STX || c == ETX || c == ACK)
        c += 2;

    return c;
}

// Target temperature is encoded as one character
static inline float s21_decode_target_temp(unsigned char v) {
    return 18.0 + 0.5 * ((signed)v - AC_MIN_TEMP_VALUE);
}

static inline uint8_t s21_encode_target_temp(float temp) {
    return (uint8_t)(lroundf((temp - 18.0) * 2) + AC_MIN_TEMP_VALUE);
}

static inline int s21_decode_int_sensor(const unsigned char* payload) {
    int v = (payload[0] - '0') + (payload[1] - '0') * 10 + (payload[2] - '0') * 100;
    if (payload[3] == '-')
        v = -v;
    return v;
}

static inline uint16_t s21_decode_hex_sensor(const unsigned char* payload) {
#define hex(c) (((c)&0xF)+((c)>'9'?9:0))
    return (hex(payload[3]) << 12) | (hex(payload[2]) << 8) | (hex(payload[1]) << 4) | hex(payload[0]);
#undef hex
}

static inline float s21_decode_float_sensor(const unsigned char* payload) {
    return (float)s21_decode_int_sensor(payload) * 0.1;
}

// Convert between Daikin and Faikin fan speed enums
static inline unsigned char s21_encode_fan(int speed) {
    return convertFanSpeedToAC(speed);
}

static inline int s21_decode_fan(unsigned char v) {
    return convertACToFanSpeed(v);
}

// ========== 高級數據編碼/解碼器 (Phase 2) ==========

// BCD (Binary-Coded Decimal) 編碼/解碼
static inline uint8_t s21_encode_bcd(uint8_t value) {
    if (value > 99) return 0xFF;  // 錯誤值
    return ((value / 10) << 4) | (value % 10);
}

static inline uint8_t s21_decode_bcd(uint8_t bcd) {
    uint8_t high = (bcd >> 4) & 0x0F;
    uint8_t low = bcd & 0x0F;
    if (high > 9 || low > 9) return 0xFF;  // 無效 BCD
    return high * 10 + low;
}

// 多字節整數編碼/解碼（小端序）
static inline uint16_t s21_decode_uint16_le(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static inline uint32_t s21_decode_uint32_le(const uint8_t* data) {
    return (uint32_t)data[0] | 
           ((uint32_t)data[1] << 8) | 
           ((uint32_t)data[2] << 16) | 
           ((uint32_t)data[3] << 24);
}

static inline void s21_encode_uint16_le(uint16_t value, uint8_t* data) {
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
}

static inline void s21_encode_uint32_le(uint32_t value, uint8_t* data) {
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    data[2] = (value >> 16) & 0xFF;
    data[3] = (value >> 24) & 0xFF;
}

// 多字節整數編碼/解碼（大端序）
static inline uint16_t s21_decode_uint16_be(const uint8_t* data) {
    return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

static inline uint32_t s21_decode_uint32_be(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | 
           ((uint32_t)data[1] << 16) | 
           ((uint32_t)data[2] << 8) | 
           (uint32_t)data[3];
}

static inline void s21_encode_uint16_be(uint16_t value, uint8_t* data) {
    data[0] = (value >> 8) & 0xFF;
    data[1] = value & 0xFF;
}

static inline void s21_encode_uint32_be(uint32_t value, uint8_t* data) {
    data[0] = (value >> 24) & 0xFF;
    data[1] = (value >> 16) & 0xFF;
    data[2] = (value >> 8) & 0xFF;
    data[3] = value & 0xFF;
}

// 定點數編碼/解碼（1/10 精度）
static inline float s21_decode_fixed_point_10(uint16_t value) {
    return (float)value / 10.0f;
}

static inline uint16_t s21_encode_fixed_point_10(float value) {
    return (uint16_t)(value * 10.0f + 0.5f);  // 四捨五入
}

// 定點數編碼/解碼（1/100 精度）
static inline float s21_decode_fixed_point_100(uint16_t value) {
    return (float)value / 100.0f;
}

static inline uint16_t s21_encode_fixed_point_100(float value) {
    return (uint16_t)(value * 100.0f + 0.5f);  // 四捨五入
}

// 溫度編碼/解碼增強版（支援負溫度和高精度）
static inline float s21_decode_temperature_enhanced(const uint8_t* data, uint8_t format) {
    switch (format) {
        case 0: // 標準格式：單字節，18°C + 0.5°C * value
            return s21_decode_target_temp(data[0]);
            
        case 1: // BCD 格式：兩字節 BCD，直接表示溫度 * 10
        {
            uint8_t high = s21_decode_bcd(data[0]);
            uint8_t low = s21_decode_bcd(data[1]);
            if (high == 0xFF || low == 0xFF) return -999.0f;  // 錯誤標誌
            return (float)(high * 100 + low) / 10.0f;
        }
        
        case 2: // 16位有符號整數，0.1°C 精度
        {
            int16_t temp = (int16_t)s21_decode_uint16_le(data);
            return (float)temp / 10.0f;
        }
        
        case 3: // 16位無符號整數，0.1°C 精度，偏移 -40°C
        {
            uint16_t temp = s21_decode_uint16_le(data);
            return ((float)temp / 10.0f) - 40.0f;
        }
        
        default:
            return -999.0f;  // 未知格式
    }
}

static inline bool s21_encode_temperature_enhanced(float temperature, uint8_t* data, uint8_t format) {
    switch (format) {
        case 0: // 標準格式
            data[0] = s21_encode_target_temp(temperature);
            return true;
            
        case 1: // BCD 格式
        {
            if (temperature < 0.0f || temperature > 99.9f) return false;
            uint16_t temp_x10 = (uint16_t)(temperature * 10.0f + 0.5f);
            data[0] = s21_encode_bcd(temp_x10 / 100);
            data[1] = s21_encode_bcd(temp_x10 % 100);
            return true;
        }
        
        case 2: // 16位有符號整數
        {
            if (temperature < -3276.7f || temperature > 3276.7f) return false;
            int16_t temp = (int16_t)(temperature * 10.0f);
            s21_encode_uint16_le((uint16_t)temp, data);
            return true;
        }
        
        case 3: // 16位無符號整數，偏移 -40°C
        {
            float offset_temp = temperature + 40.0f;
            if (offset_temp < 0.0f || offset_temp > 6553.5f) return false;
            uint16_t temp = (uint16_t)(offset_temp * 10.0f + 0.5f);
            s21_encode_uint16_le(temp, data);
            return true;
        }
        
        default:
            return false;
    }
}

// 濕度編碼/解碼（支援多種格式）
static inline float s21_decode_humidity(const uint8_t* data, uint8_t format) {
    switch (format) {
        case 0: // 單字節百分比（0-100）
            return (float)data[0];
            
        case 1: // 單字節，0.5% 精度
            return (float)data[0] / 2.0f;
            
        case 2: // 16位，0.1% 精度
            return s21_decode_fixed_point_10(s21_decode_uint16_le(data));
            
        case 3: // BCD 格式，直接表示百分比
        {
            uint8_t humidity = s21_decode_bcd(data[0]);
            return (humidity == 0xFF) ? -1.0f : (float)humidity;
        }
        
        default:
            return -1.0f;  // 錯誤值
    }
}

static inline bool s21_encode_humidity(float humidity, uint8_t* data, uint8_t format) {
    if (humidity < 0.0f || humidity > 100.0f) return false;
    
    switch (format) {
        case 0: // 單字節百分比
            data[0] = (uint8_t)(humidity + 0.5f);
            return true;
            
        case 1: // 0.5% 精度
            data[0] = (uint8_t)(humidity * 2.0f + 0.5f);
            return true;
            
        case 2: // 0.1% 精度
            s21_encode_uint16_le(s21_encode_fixed_point_10(humidity), data);
            return true;
            
        case 3: // BCD 格式
            data[0] = s21_encode_bcd((uint8_t)(humidity + 0.5f));
            return data[0] != 0xFF;
            
        default:
            return false;
    }
}

// 功率/電流/電壓編碼解碼
static inline float s21_decode_power(const uint8_t* data, uint8_t format) {
    switch (format) {
        case 0: // 16位，1W 精度
            return (float)s21_decode_uint16_le(data);
            
        case 1: // 16位，0.1W 精度
            return s21_decode_fixed_point_10(s21_decode_uint16_le(data));
            
        case 2: // 32位，0.01W 精度
            return s21_decode_fixed_point_100(s21_decode_uint32_le(data));
            
        default:
            return -1.0f;
    }
}

static inline float s21_decode_current(const uint8_t* data, uint8_t format) {
    switch (format) {
        case 0: // 16位，0.1A 精度
            return s21_decode_fixed_point_10(s21_decode_uint16_le(data));
            
        case 1: // 16位，0.01A 精度
            return s21_decode_fixed_point_100(s21_decode_uint16_le(data));
            
        default:
            return -1.0f;
    }
}

static inline float s21_decode_voltage(const uint8_t* data, uint8_t format) {
    switch (format) {
        case 0: // 16位，1V 精度
            return (float)s21_decode_uint16_le(data);
            
        case 1: // 16位，0.1V 精度
            return s21_decode_fixed_point_10(s21_decode_uint16_le(data));
            
        default:
            return -1.0f;
    }
}

// 時間編碼/解碼（多種格式）
struct S21DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

static inline bool s21_decode_datetime(const uint8_t* data, uint8_t format, S21DateTime& dt) {
    switch (format) {
        case 0: // 6字節 BCD 格式：YY MM DD HH MM SS
            dt.year = 2000 + s21_decode_bcd(data[0]);
            dt.month = s21_decode_bcd(data[1]);
            dt.day = s21_decode_bcd(data[2]);
            dt.hour = s21_decode_bcd(data[3]);
            dt.minute = s21_decode_bcd(data[4]);
            dt.second = s21_decode_bcd(data[5]);
            return (dt.month <= 12 && dt.day <= 31 && dt.hour <= 23 && 
                    dt.minute <= 59 && dt.second <= 59);
            
        case 1: // 4字節：32位時間戳
        {
            uint32_t timestamp = s21_decode_uint32_le(data);
            // 簡化的時間戳轉換（假設從2000年開始計算）
            dt.second = timestamp % 60; timestamp /= 60;
            dt.minute = timestamp % 60; timestamp /= 60;
            dt.hour = timestamp % 24; timestamp /= 24;
            // 簡化的日期計算
            dt.day = (timestamp % 365) % 31 + 1;
            dt.month = ((timestamp % 365) / 31) + 1;
            dt.year = 2000 + (timestamp / 365);
            return true;
        }
        
        default:
            return false;
    }
}

// 數據驗證函數
static inline bool s21_validate_temperature(float temp) {
    return (temp >= -50.0f && temp <= 80.0f);
}

static inline bool s21_validate_humidity(float humidity) {
    return (humidity >= 0.0f && humidity <= 100.0f);
}

static inline bool s21_validate_power(float power) {
    return (power >= 0.0f && power <= 50000.0f);  // 最大50kW
}

static inline bool s21_validate_voltage(float voltage) {
    return (voltage >= 0.0f && voltage <= 500.0f);  // 最大500V
}

static inline bool s21_validate_current(float current) {
    return (current >= 0.0f && current <= 100.0f);  // 最大100A
} 