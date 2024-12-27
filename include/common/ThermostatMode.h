#pragma once

// HomeKit 恆溫器模式定義
#define HAP_MODE_OFF   0
#define HAP_MODE_HEAT  1
#define HAP_MODE_COOL  2
#define HAP_MODE_AUTO  3

// 空調模式定義（與 S21Protocol.h 保持一致）
#define AC_MODE_AUTO 0
#define AC_MODE_HEAT 1
#define AC_MODE_COOL 2
#define AC_MODE_DRY  7
#define AC_MODE_FAN  6

// 風扇速度定義
#define FAN_AUTO    0  // 自動
#define FAN_QUIET   6  // 安靜
#define FAN_SPEED_1 1  // 1檔
#define FAN_SPEED_2 2  // 2檔
#define FAN_SPEED_3 3  // 3檔
#define FAN_SPEED_4 4  // 4檔
#define FAN_SPEED_5 5  // 5檔

// 模式轉換函數聲明
static uint8_t convertHomeKitToACMode(uint8_t hapMode);
static uint8_t convertACToHomeKitMode(uint8_t acMode, bool isPowerOn);

// 風扇速度轉換函數聲明
static uint8_t convertFanSpeedToAC(uint8_t speed) {
    switch (speed) {
        case FAN_AUTO:    return 'A';  // 自動
        case FAN_QUIET:   return 'B';  // 安靜
        case FAN_SPEED_1: return '3';  // 1檔
        case FAN_SPEED_2: return '4';  // 2檔
        case FAN_SPEED_3: return '5';  // 3檔
        case FAN_SPEED_4: return '6';  // 4檔
        case FAN_SPEED_5: return '7';  // 5檔
        default:          return 'A';  // 默認自動
    }
}

static uint8_t convertACToFanSpeed(uint8_t acFan) {
    switch (acFan) {
        case 'A': return FAN_AUTO;    // 自動
        case 'B': return FAN_QUIET;   // 安靜
        case '3': return FAN_SPEED_1; // 1檔
        case '4': return FAN_SPEED_2; // 2檔
        case '5': return FAN_SPEED_3; // 3檔
        case '6': return FAN_SPEED_4; // 4檔
        case '7': return FAN_SPEED_5; // 5檔
        default:  return FAN_AUTO;    // 默認自動
    }
}

static const char* getFanSpeedText(uint8_t speed) {
    switch (speed) {
        case FAN_AUTO:    return "自動";
        case FAN_QUIET:   return "安靜";
        case FAN_SPEED_1: return "1檔";
        case FAN_SPEED_2: return "2檔";
        case FAN_SPEED_3: return "3檔";
        case FAN_SPEED_4: return "4檔";
        case FAN_SPEED_5: return "5檔";
        default:          return "未知";
    }
} 