#pragma once

#include <Arduino.h>
#include <vector>
#include <mutex>
#include "Debug.h"

// 日誌級別定義
enum class LogLevel {
    VERBOSE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
    CRITICAL = 5
};

// 日誌條目結構
struct LogEntry {
    unsigned long timestamp;
    LogLevel level;
    String component;
    String message;
    
    LogEntry(LogLevel lvl, const String& comp, const String& msg) :
        timestamp(millis()),
        level(lvl),
        component(comp),
        message(msg) {}
};

class LogManager {
private:
    static LogManager* instance;
    std::vector<LogEntry> logBuffer;
    std::mutex logMutex;
    size_t maxLogEntries;
    LogLevel currentLogLevel;
    bool enableSerial;
    bool enableWebLog;
    
public:
    // 日誌級別字符串 (移到 public)
    const char* getLevelString(LogLevel level) {
        switch (level) {
            case LogLevel::VERBOSE: return "VERBOSE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARNING";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::CRITICAL: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }

private:
    
    // 日誌級別顏色（用於 Web 顯示）
    const char* getLevelColor(LogLevel level) {
        switch (level) {
            case LogLevel::VERBOSE: return "#888888";
            case LogLevel::DEBUG: return "#0066cc";
            case LogLevel::INFO: return "#009900";
            case LogLevel::WARNING: return "#ff9900";
            case LogLevel::ERROR: return "#cc0000";
            case LogLevel::CRITICAL: return "#990000";
            default: return "#000000";
        }
    }
    
    LogManager() : 
        maxLogEntries(100),
        currentLogLevel(LogLevel::INFO),
        enableSerial(true),
        enableWebLog(true) {
        logBuffer.reserve(maxLogEntries);
    }
    
public:
    // 單例模式
    static LogManager& getInstance() {
        if (!instance) {
            instance = new LogManager();
        }
        return *instance;
    }
    
    // 配置日誌系統
    void configure(LogLevel level = LogLevel::INFO, size_t maxEntries = 100, 
                   bool serial = true, bool web = true) {
        std::lock_guard<std::mutex> lock(logMutex);
        currentLogLevel = level;
        maxLogEntries = maxEntries;
        enableSerial = serial;
        enableWebLog = web;
        
        // 調整緩衝區大小
        if (logBuffer.capacity() != maxLogEntries) {
            logBuffer.clear();
            logBuffer.reserve(maxLogEntries);
        }
    }
    
    // 記錄日誌
    void log(LogLevel level, const String& component, const String& message) {
        // 檢查日誌級別
        if (level < currentLogLevel) {
            return;
        }
        
        // 創建日誌條目
        LogEntry entry(level, component, message);
        
        // 輸出到串口
        if (enableSerial) {
            Serial.printf("[%lu][%s][%s] %s\n", 
                         entry.timestamp, 
                         getLevelString(level), 
                         component.c_str(), 
                         message.c_str());
        }
        
        // 添加到緩衝區
        if (enableWebLog) {
            std::lock_guard<std::mutex> lock(logMutex);
            
            // 如果緩衝區滿了，移除最舊的條目
            if (logBuffer.size() >= maxLogEntries) {
                logBuffer.erase(logBuffer.begin());
            }
            
            logBuffer.push_back(entry);
        }
    }
    
    // 便捷的日誌記錄方法
    void verbose(const String& component, const String& message) {
        log(LogLevel::VERBOSE, component, message);
    }
    
    void debug(const String& component, const String& message) {
        log(LogLevel::DEBUG, component, message);
    }
    
    void info(const String& component, const String& message) {
        log(LogLevel::INFO, component, message);
    }
    
    void warning(const String& component, const String& message) {
        log(LogLevel::WARNING, component, message);
    }
    
    void error(const String& component, const String& message) {
        log(LogLevel::ERROR, component, message);
    }
    
    void critical(const String& component, const String& message) {
        log(LogLevel::CRITICAL, component, message);
    }
    
    // 格式化日誌記錄
    void logf(LogLevel level, const String& component, const char* format, ...) {
        va_list args;
        va_start(args, format);
        
        char buffer[512];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        log(level, component, String(buffer));
    }
    
    // 獲取日誌條目
    std::vector<LogEntry> getLogs() {
        std::lock_guard<std::mutex> lock(logMutex);
        return logBuffer;
    }
    
    // 清除日誌
    void clearLogs() {
        std::lock_guard<std::mutex> lock(logMutex);
        logBuffer.clear();
    }
    
    // 獲取日誌統計
    struct LogStats {
        size_t totalEntries;
        size_t verboseCount;
        size_t debugCount;
        size_t infoCount;
        size_t warningCount;
        size_t errorCount;
        size_t criticalCount;
    };
    
    LogStats getStats() {
        std::lock_guard<std::mutex> lock(logMutex);
        LogStats stats = {0};
        stats.totalEntries = logBuffer.size();
        
        for (const auto& entry : logBuffer) {
            switch (entry.level) {
                case LogLevel::VERBOSE: stats.verboseCount++; break;
                case LogLevel::DEBUG: stats.debugCount++; break;
                case LogLevel::INFO: stats.infoCount++; break;
                case LogLevel::WARNING: stats.warningCount++; break;
                case LogLevel::ERROR: stats.errorCount++; break;
                case LogLevel::CRITICAL: stats.criticalCount++; break;
            }
        }
        
        return stats;
    }
    
    // 生成 HTML 日誌頁面 (記憶體優化版本)
    String generateLogHTML() {
        std::lock_guard<std::mutex> lock(logMutex);
        LogStats stats = getStats();

        const size_t MAX_DISPLAY_ENTRIES = 20;
        size_t displayCount = std::min(logBuffer.size(), MAX_DISPLAY_ENTRIES);

        // 分配一個足夠大的緩衝區來構建HTML，避免String串接造成的記憶體碎片。
        const size_t bufferSize = 3072;
        auto buffer = std::make_unique<char[]>(bufferSize);
        if (!buffer) {
            log(LogLevel::CRITICAL, "LogManager", "Failed to allocate memory for HTML log page.");
            return "<!DOCTYPE html><html><body><h1>Error: Not enough memory to display logs.</h1></body></html>";
        }

        char* p = buffer.get();
        int remaining = bufferSize;
        int written;

        // 安全地附加內容到緩衝區的輔助 Lambda
        auto append = [&](const char* format, ...) {
            if (remaining <= 1) return;
            va_list args;
            va_start(args, format);
            written = vsnprintf(p, remaining, format, args);
            va_end(args);
            if (written > 0) {
                p += written;
                remaining -= written;
            }
        };

        append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\">");
        append("<title>DaiSpan Logs</title>");
        append("<style>body{font-family:monospace;margin:10px;background:#f0f0f0;}");
        append(".container{max-width:800px;margin:0 auto;background:white;padding:15px;border-radius:5px;}");
        append("h1{color:#333;text-align:center;margin:10px;}");
        append(".button{padding:5px 10px;margin:3px;background:#007cba;color:white;text-decoration:none;border-radius:3px;}");
        append(".stats{background:#e8f4f8;padding:10px;border-radius:3px;margin:10px 0;}");
        append(".log-container{background:#000;color:#0f0;padding:10px;border-radius:3px;max-height:300px;overflow-y:auto;font-size:11px;}");
        append("</style></head><body><div class=\"container\">");
        append("<h1>DaiSpan System Logs</h1>");
        
        append("<div style=\"text-align:center;\">");
        append("<button onclick=\"location.reload()\" class=\"button\">Refresh</button>");
        append("<button onclick=\"clearLogs()\" class=\"button\">Clear</button>");
        append("<a href=\"/\" class=\"button\">Back</a></div>");
        
        append("<div class=\"stats\"><h3>Stats</h3><p>Total: %d", stats.totalEntries);
        if (displayCount < stats.totalEntries) {
            append(" (latest %d)", displayCount);
        }
        append(" | INFO: %d", stats.infoCount);
        append(" | WARN: %d", stats.warningCount);
        append(" | ERR: %d</p></div>", stats.errorCount);
        
        append("<div class=\"log-container\">");
        if (logBuffer.size() > 0) {
            size_t startIndex = logBuffer.size() > displayCount ? logBuffer.size() - displayCount : 0;
            for (size_t i = startIndex; i < logBuffer.size(); i++) {
                if (remaining < 256) { // 為最後的日誌條目保留足夠空間，避免溢出
                    append("...<br>");
                    break;
                }
                const auto& entry = logBuffer[i];
                // 注意：此處未對 entry.message 進行HTML轉義，假設日誌內容是安全的
                append("[%lu][%s][%s] %s<br>", entry.timestamp, getLevelString(entry.level), entry.component.c_str(), entry.message.c_str());
            }
        } else {
            append("No logs");
        }
        append("</div>");
        
        append("<script>function clearLogs(){if(confirm('Clear?')){fetch('/logs-clear',{method:'POST'}).then(()=>location.reload());}}</script>");
        append("</div></body></html>");
        
        return String(buffer.get());
    }
    
    // 生成 JSON 格式的日誌
    String generateLogJSON() {
        std::lock_guard<std::mutex> lock(logMutex);
        
        // 限制 JSON 返回的條目數量
        const size_t MAX_JSON_ENTRIES = 100;
        size_t startIndex = logBuffer.size() > MAX_JSON_ENTRIES ? logBuffer.size() - MAX_JSON_ENTRIES : 0;
        
        String json = "{\"logs\":[";
        bool first = true;
        for (size_t i = startIndex; i < logBuffer.size(); i++) {
            if (!first) json += ",";
            first = false;
            
            const auto& entry = logBuffer[i];
            json += "{";
            json += "\"timestamp\":" + String(entry.timestamp) + ",";
            json += "\"level\":\"" + String(getLevelString(entry.level)) + "\",";
            json += "\"component\":\"" + entry.component + "\",";
            json += "\"message\":\"" + entry.message + "\"";
            json += "}";
        }
        json += "]}";
        
        return json;
    }
};

// 靜態實例指針
LogManager* LogManager::instance = nullptr;

// 全局日誌實例
#define LOG_MANAGER LogManager::getInstance()

// 便捷的日誌宏
#define LOG_VERBOSE(component, message) LOG_MANAGER.verbose(component, message)
#define LOG_DEBUG(component, message) LOG_MANAGER.debug(component, message)
#define LOG_INFO(component, message) LOG_MANAGER.info(component, message)
#define LOG_WARNING(component, message) LOG_MANAGER.warning(component, message)
#define LOG_ERROR(component, message) LOG_MANAGER.error(component, message)
#define LOG_CRITICAL(component, message) LOG_MANAGER.critical(component, message)

// 格式化日誌宏
#define LOG_INFOF(component, format, ...) LOG_MANAGER.logf(LogLevel::INFO, component, format, ##__VA_ARGS__)
#define LOG_WARNINGF(component, format, ...) LOG_MANAGER.logf(LogLevel::WARNING, component, format, ##__VA_ARGS__)
#define LOG_ERRORF(component, format, ...) LOG_MANAGER.logf(LogLevel::ERROR, component, format, ##__VA_ARGS__)