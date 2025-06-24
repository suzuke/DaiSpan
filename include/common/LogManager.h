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
    
    // 生成 HTML 日誌頁面
    String generateLogHTML() {
        std::lock_guard<std::mutex> lock(logMutex);
        LogStats stats = getStats();
        
        // 進一步減少顯示條目數量
        const size_t MAX_DISPLAY_ENTRIES = 20;
        size_t displayCount = std::min(logBuffer.size(), MAX_DISPLAY_ENTRIES);
        
        // 使用更簡單的 HTML 結構
        String html;
        html.reserve(2048); // 預分配記憶體
        
        html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
        html += "<title>DaiSpan Logs</title>";
        html += "<style>body{font-family:monospace;margin:10px;background:#f0f0f0;}";
        html += ".container{max-width:800px;margin:0 auto;background:white;padding:15px;border-radius:5px;}";
        html += "h1{color:#333;text-align:center;margin:10px;}";
        html += ".button{padding:5px 10px;margin:3px;background:#007cba;color:white;text-decoration:none;border-radius:3px;}";
        html += ".stats{background:#e8f4f8;padding:10px;border-radius:3px;margin:10px 0;}";
        html += ".log-container{background:#000;color:#0f0;padding:10px;border-radius:3px;max-height:300px;overflow-y:auto;font-size:11px;}";
        html += "</style></head><body><div class=\"container\">";
        html += "<h1>DaiSpan System Logs</h1>";
        
        // 控制按鈕
        html += "<div style=\"text-align:center;\">";
        html += "<button onclick=\"location.reload()\" class=\"button\">Refresh</button>";
        html += "<button onclick=\"clearLogs()\" class=\"button\">Clear</button>";
        html += "<a href=\"/\" class=\"button\">Back</a></div>";
        
        // 統計信息
        html += "<div class=\"stats\"><h3>Stats</h3><p>Total: " + String(stats.totalEntries);
        if (displayCount < stats.totalEntries) {
            html += " (latest " + String(displayCount) + ")";
        }
        html += " | INFO: " + String(stats.infoCount);
        html += " | WARN: " + String(stats.warningCount);
        html += " | ERR: " + String(stats.errorCount) + "</p></div>";
        
        // 日誌內容 - 只顯示最新的條目
        html += "<div class=\"log-container\">";
        if (logBuffer.size() > 0) {
            size_t startIndex = logBuffer.size() > displayCount ? logBuffer.size() - displayCount : 0;
            for (size_t i = startIndex; i < logBuffer.size(); i++) {
                const auto& entry = logBuffer[i];
                html += "[" + String(entry.timestamp) + "][" + String(getLevelString(entry.level)) + "][" + entry.component + "] " + entry.message + "<br>";
            }
        } else {
            html += "No logs";
        }
        html += "</div>";
        
        // JavaScript
        html += "<script>function clearLogs(){if(confirm('Clear?')){fetch('/logs-clear',{method:'POST'}).then(()=>location.reload());}}</script>";
        html += "</div></body></html>";
        
        return html;
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