#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "MemoryOptimization.h"

// 統一記憶體監控系統 - 適用於所有調試環境
class MemoryMonitor {
private:
    // 記憶體歷史追蹤
    struct MemoryHistory {
        static constexpr size_t HISTORY_SIZE = 60; // 保留60個樣本
        uint32_t timestamps[HISTORY_SIZE];
        uint32_t freeHeap[HISTORY_SIZE];
        uint8_t currentIndex = 0;
        uint8_t sampleCount = 0;
        
        void addSample(uint32_t timestamp, uint32_t heap) {
            timestamps[currentIndex] = timestamp;
            freeHeap[currentIndex] = heap;
            currentIndex = (currentIndex + 1) % HISTORY_SIZE;
            if (sampleCount < HISTORY_SIZE) sampleCount++;
        }
    };
    
    MemoryHistory history;
    MemoryOptimization::MemoryManager memoryManager;
    
    // 統計數據
    uint32_t lastUpdateTime = 0;
    uint32_t updateInterval = 5000; // 5秒更新一次
    
public:
    // 記憶體健康狀態
    enum class HealthStatus {
        EXCELLENT,  // >150KB
        GOOD,       // 100-150KB
        WARNING,    // 50-100KB
        CRITICAL,   // 30-50KB
        EMERGENCY   // <30KB
    };
    
    // 初始化監控器
    void begin() {
        // 記錄初始狀態
        updateMemoryStats();
    }
    
    // 更新記憶體統計 - 在主循環中調用
    void loop() {
        uint32_t currentTime = millis();
        if (currentTime - lastUpdateTime >= updateInterval) {
            updateMemoryStats();
            lastUpdateTime = currentTime;
        }
    }
    
    // 獲取當前記憶體健康狀態
    HealthStatus getHealthStatus() const {
        uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap >= 150000) return HealthStatus::EXCELLENT;
        if (freeHeap >= 100000) return HealthStatus::GOOD;
        if (freeHeap >= 50000) return HealthStatus::WARNING;
        if (freeHeap >= 30000) return HealthStatus::CRITICAL;
        return HealthStatus::EMERGENCY;
    }
    
    // 獲取健康狀態文字
    const char* getHealthStatusText() const {
        switch (getHealthStatus()) {
            case HealthStatus::EXCELLENT: return "EXCELLENT";
            case HealthStatus::GOOD: return "GOOD";
            case HealthStatus::WARNING: return "WARNING";
            case HealthStatus::CRITICAL: return "CRITICAL";
            case HealthStatus::EMERGENCY: return "EMERGENCY";
            default: return "UNKNOWN";
        }
    }
    
    // 獲取健康狀態CSS類別
    const char* getHealthStatusClass() const {
        switch (getHealthStatus()) {
            case HealthStatus::EXCELLENT: return "status-excellent";
            case HealthStatus::GOOD: return "status-good";
            case HealthStatus::WARNING: return "status-warn";
            case HealthStatus::CRITICAL: return "status-error";
            case HealthStatus::EMERGENCY: return "status-critical";
            default: return "status-unknown";
        }
    }
    
    // 獲取記憶體壓力等級
    MemoryOptimization::MemoryManager::MemoryPressure getMemoryPressure() {
        return memoryManager.updateMemoryPressure();
    }
    
    // 獲取當前記憶體使用率百分比
    float getMemoryUsagePercent() const {
        // ESP32-C3總記憶體約327KB
        uint32_t totalMemory = 327680;
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t usedMemory = totalMemory - freeHeap;
        return (float)usedMemory / totalMemory * 100.0f;
    }
    
    // 獲取記憶體統計的JSON字串
    String getMemoryStatsJson() const {
        JsonDocument doc;
        
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t totalMemory = 327680; // ESP32-C3
        
        doc["current"]["free_heap"] = freeHeap;
        doc["current"]["free_heap_kb"] = freeHeap / 1024;
        doc["current"]["used_memory"] = totalMemory - freeHeap;
        doc["current"]["usage_percent"] = getMemoryUsagePercent();
        doc["current"]["health_status"] = getHealthStatusText();
        doc["current"]["health_class"] = getHealthStatusClass();
        
        // 歷史統計
        if (history.sampleCount > 0) {
            uint32_t min_heap = UINT32_MAX;
            uint32_t max_heap = 0;
            uint64_t sum_heap = 0;
            
            for (uint8_t i = 0; i < history.sampleCount; i++) {
                uint32_t heap = history.freeHeap[i];
                min_heap = min(min_heap, heap);
                max_heap = max(max_heap, heap);
                sum_heap += heap;
            }
            
            doc["history"]["min_heap"] = min_heap;
            doc["history"]["max_heap"] = max_heap;
            doc["history"]["avg_heap"] = (uint32_t)(sum_heap / history.sampleCount);
            doc["history"]["samples"] = history.sampleCount;
        }
        
        // 系統信息
        doc["system"]["total_memory"] = totalMemory;
        doc["system"]["chip_model"] = "ESP32-C3";
        doc["system"]["cpu_freq"] = ESP.getCpuFreqMHz();
        doc["system"]["flash_size"] = ESP.getFlashChipSize();
        doc["system"]["uptime"] = millis();
        
        String result;
        serializeJson(doc, result);
        return result;
    }
    
    // 獲取簡化的記憶體HTML報告
    String getMemoryHtmlReport() const {
        String html;
        uint32_t freeHeap = ESP.getFreeHeap();
        float usagePercent = getMemoryUsagePercent();
        
        html += "<div class='memory-report'>";
        html += "<h3>💾 記憶體監控</h3>";
        
        // 當前狀態
        html += "<div class='metric'>";
        html += "<span>可用記憶體:</span>";
        html += "<span class='" + String(getHealthStatusClass()) + "'>";
        html += String(freeHeap / 1024) + " KB (" + String(100.0f - usagePercent, 1) + "% 可用)";
        html += "</span></div>";
        
        html += "<div class='metric'>";
        html += "<span>健康狀態:</span>";
        html += "<span class='" + String(getHealthStatusClass()) + "'>" + getHealthStatusText() + "</span>";
        html += "</div>";
        
        // 記憶體使用率條
        html += "<div class='progress-container'>";
        html += "<div class='progress-bar' style='width: " + String(usagePercent, 1) + "%; ";
        String progressBarColor = getProgressBarColor(usagePercent);
        html += "background-color: " + progressBarColor + ";'></div>";
        html += "<div class='progress-text'>" + String(usagePercent, 1) + "% 已使用</div>";
        html += "</div>";
        
        // 歷史統計
        if (history.sampleCount > 0) {
            uint32_t min_heap = UINT32_MAX;
            uint32_t max_heap = 0;
            
            for (uint8_t i = 0; i < history.sampleCount; i++) {
                uint32_t heap = history.freeHeap[i];
                min_heap = min(min_heap, heap);
                max_heap = max(max_heap, heap);
            }
            
            html += "<div class='metric'>";
            html += "<span>記憶體範圍:</span>";
            html += "<span>" + String(min_heap/1024) + " - " + String(max_heap/1024) + " KB</span>";
            html += "</div>";
        }
        
        html += "</div>";
        
        // CSS樣式
        html += "<style>";
        html += ".memory-report { margin: 10px 0; }";
        html += ".metric { display: flex; justify-content: space-between; margin: 8px 0; padding: 8px; background: #f8f9fa; border-radius: 6px; }";
        html += ".status-excellent { color: #28a745; font-weight: bold; }";
        html += ".status-good { color: #20c997; font-weight: bold; }";
        html += ".status-warn { color: #ffc107; font-weight: bold; }";
        html += ".status-error { color: #fd7e14; font-weight: bold; }";
        html += ".status-critical { color: #dc3545; font-weight: bold; animation: blink 1s infinite; }";
        html += ".progress-container { position: relative; width: 100%; height: 20px; background: #e9ecef; border-radius: 10px; margin: 8px 0; }";
        html += ".progress-bar { height: 100%; border-radius: 10px; transition: width 0.3s; }";
        html += ".progress-text { position: absolute; width: 100%; text-align: center; line-height: 20px; font-size: 12px; color: #333; }";
        html += "@keyframes blink { 0%, 50% { opacity: 1; } 51%, 100% { opacity: 0.5; } }";
        html += "</style>";
        
        return html;
    }
    
    // 獲取記憶體警告（如果有的話）
    String getMemoryWarning() const {
        HealthStatus status = getHealthStatus();
        
        switch (status) {
            case HealthStatus::WARNING:
                return "⚠️ 記憶體使用率較高，建議關閉不必要的功能";
            case HealthStatus::CRITICAL:
                return "🔴 記憶體嚴重不足，系統可能不穩定";
            case HealthStatus::EMERGENCY:
                return "🚨 記憶體極度不足，請立即重啟設備";
            default:
                return "";
        }
    }
    
    // 檢查是否需要緊急清理
    bool needsEmergencyCleanup() const {
        return getHealthStatus() == HealthStatus::EMERGENCY;
    }
    
    // 獲取建議的記憶體優化操作
    String getOptimizationSuggestions() const {
        String suggestions;
        HealthStatus status = getHealthStatus();
        
        if (status >= HealthStatus::WARNING) {
            suggestions += "• 關閉調試模式以節省記憶體\n";
            suggestions += "• 減少日誌緩衝區大小\n";
            suggestions += "• 避免同時運行多個Web請求\n";
        }
        
        if (status >= HealthStatus::CRITICAL) {
            suggestions += "• 重啟設備以釋放記憶體\n";
            suggestions += "• 檢查是否有記憶體洩漏\n";
            suggestions += "• 使用精簡版界面\n";
        }
        
        return suggestions;
    }
    
private:
    void updateMemoryStats() {
        uint32_t currentTime = millis();
        uint32_t freeHeap = ESP.getFreeHeap();
        
        // 更新歷史記錄
        history.addSample(currentTime, freeHeap);
        
        // 更新記憶體管理器
        memoryManager.updateMemoryPressure();
    }
    
    const char* getProgressBarColor(float usagePercent) const {
        if (usagePercent <= 50.0f) return "#28a745"; // 綠色
        if (usagePercent <= 70.0f) return "#ffc107"; // 黃色
        if (usagePercent <= 85.0f) return "#fd7e14"; // 橙色
        return "#dc3545"; // 紅色
    }
};