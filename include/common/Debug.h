#pragma once

#include "HomeSpan.h"

// 前向聲明遠端調試功能
void remoteWebLog(const String& message);

#define DEBUG_MODE

// 心跳間隔
#define HEARTBEAT_INTERVAL 5000

// 定義調試級別常量
#define DEBUG_NONE      0  // 無調試訊息
#define DEBUG_ERROR     1  // 只顯示錯誤
#define DEBUG_INFO      2  // 顯示重要狀態更新
#define DEBUG_VERBOSE   3  // 顯示詳細通訊過程

// 性能優化：生產環境使用較低的調試級別
#ifdef PRODUCTION_BUILD
#define DEBUG_LEVEL DEBUG_ERROR  // 生產環境只顯示錯誤
#else
#define DEBUG_LEVEL DEBUG_INFO   // 開發環境顯示資訊
#endif

#define DEBUG_BUFFER_SIZE 256

#define DEBUG_ERROR_PRINT(...) do { if (DEBUG_LEVEL >= DEBUG_ERROR) { Serial.printf(__VA_ARGS__); char buffer[DEBUG_BUFFER_SIZE]; snprintf(buffer, DEBUG_BUFFER_SIZE, __VA_ARGS__); remoteWebLog(buffer); } } while(0)
#define DEBUG_WARN_PRINT(...) do { if (DEBUG_LEVEL >= DEBUG_ERROR) { Serial.printf(__VA_ARGS__); char buffer[DEBUG_BUFFER_SIZE]; snprintf(buffer, DEBUG_BUFFER_SIZE, __VA_ARGS__); remoteWebLog(buffer); } } while(0)
#define DEBUG_INFO_PRINT(...) do { if (DEBUG_LEVEL >= DEBUG_INFO) { Serial.printf(__VA_ARGS__); char buffer[DEBUG_BUFFER_SIZE]; snprintf(buffer, DEBUG_BUFFER_SIZE, __VA_ARGS__); remoteWebLog(buffer); } } while(0)
#define DEBUG_VERBOSE_PRINT(...) do { if (DEBUG_LEVEL >= DEBUG_VERBOSE) { Serial.printf(__VA_ARGS__); char buffer[DEBUG_BUFFER_SIZE]; snprintf(buffer, DEBUG_BUFFER_SIZE, __VA_ARGS__); remoteWebLog(buffer); } } while(0)