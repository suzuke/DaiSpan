#pragma once

#include "HomeSpan.h"

// 條件編譯：遠端調試功能前向聲明
#if defined(ENABLE_REMOTE_DEBUG) || defined(ENABLE_LIGHTWEIGHT_DEBUG)
void remoteWebLog(const String& message);
#else
// 生產模式：空實現
inline void remoteWebLog(const String& message) {}
#endif

#define DEBUG_MODE

// 心跳間隔
#define HEARTBEAT_INTERVAL 5000

// 定義調試級別常量
#define DEBUG_NONE      0  // 無調試訊息
#define DEBUG_ERROR     1  // 只顯示錯誤
#define DEBUG_WARN      2  // 顯示警告
#define DEBUG_INFO      3  // 顯示重要狀態更新
#define DEBUG_VERBOSE   4  // 顯示詳細通訊過程

// 性能優化：生產環境使用較低的調試級別
#ifdef PRODUCTION_BUILD
    #ifdef MINIMAL_DEBUG_OUTPUT
    #define DEBUG_LEVEL DEBUG_NONE   // 最小調試輸出
    #else
    #define DEBUG_LEVEL DEBUG_ERROR  // 生產環境只顯示錯誤
    #endif
#else
#define DEBUG_LEVEL DEBUG_INFO   // 開發環境顯示資訊
#endif

// 優化的緩衝區大小和內存管理
#define DEBUG_BUFFER_SIZE 256
#define DEBUG_MAX_REMOTE_LOGS 10  // 遠端日誌最大緩存數量

// 優化的日誌宏 - 避免重複的內存分配
#if DEBUG_LEVEL >= DEBUG_ERROR
#define DEBUG_ERROR_PRINT(...) do { \
    Serial.printf(__VA_ARGS__); \
    static char buffer[DEBUG_BUFFER_SIZE]; \
    snprintf(buffer, DEBUG_BUFFER_SIZE, __VA_ARGS__); \
    remoteWebLog(buffer); \
} while(0)
#else
#define DEBUG_ERROR_PRINT(...) do {} while(0)
#endif

#if DEBUG_LEVEL >= DEBUG_WARN
#define DEBUG_WARN_PRINT(...) do { \
    Serial.printf(__VA_ARGS__); \
    static char buffer[DEBUG_BUFFER_SIZE]; \
    snprintf(buffer, DEBUG_BUFFER_SIZE, __VA_ARGS__); \
    remoteWebLog(buffer); \
} while(0)
#else
#define DEBUG_WARN_PRINT(...) do {} while(0)
#endif

#if DEBUG_LEVEL >= DEBUG_INFO
#define DEBUG_INFO_PRINT(...) do { \
    Serial.printf(__VA_ARGS__); \
    static char buffer[DEBUG_BUFFER_SIZE]; \
    snprintf(buffer, DEBUG_BUFFER_SIZE, __VA_ARGS__); \
    remoteWebLog(buffer); \
} while(0)
#else
#define DEBUG_INFO_PRINT(...) do {} while(0)
#endif

#if DEBUG_LEVEL >= DEBUG_VERBOSE
#define DEBUG_VERBOSE_PRINT(...) do { \
    Serial.printf(__VA_ARGS__); \
    static char buffer[DEBUG_BUFFER_SIZE]; \
    snprintf(buffer, DEBUG_BUFFER_SIZE, __VA_ARGS__); \
    remoteWebLog(buffer); \
} while(0)
#else
#define DEBUG_VERBOSE_PRINT(...) do {} while(0)
#endif