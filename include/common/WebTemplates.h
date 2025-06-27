#pragma once

#include <Arduino.h>

/**
 * 預編譯模板 - 避免動態字串拼接造成記憶體碎片
 * 目前只實現JSON API，HTML模板因中文字符編碼問題暫時回退到WebUI
 */
namespace WebTemplates {

    /**
     * JSON API 模板 - 唯一成功實現的記憶體優化
     */
    const char JSON_API_TEMPLATE[] PROGMEM = 
        "{\"wifi_ssid\":\"%s\","
        "\"wifi_ip\":\"%s\","
        "\"wifi_mac\":\"%s\","
        "\"wifi_rssi\":%d,"
        "\"wifi_gateway\":\"%s\","
        "\"free_heap\":%u,"
        "\"cpu_freq\":%u,"
        "\"flash_size\":%u,"
        "\"homekit_initialized\":%s,"
        "\"device_initialized\":%s,"
        "\"uptime\":%lu,"
        "\"chip_model\":\"%s\","
        "\"homekit_port\":1201,"
        "\"monitor_port\":8080}";
    
    /**
     * 生成JSON API響應 - 使用預編譯模板，避免String拼接造成記憶體碎片
     */
    String generateJsonApi(const String& ssid, const String& ip, const String& mac,
                          int rssi, const String& gateway, uint32_t freeHeap,
                          bool homeKitInit, bool deviceInit, unsigned long uptime) {
        
        char buffer[512];  // 固定大小緩衝區，避免動態分配
        
        snprintf_P(buffer, sizeof(buffer), JSON_API_TEMPLATE,
            ssid.c_str(), ip.c_str(), mac.c_str(), rssi, gateway.c_str(),
            freeHeap, (uint32_t)ESP.getCpuFreqMHz(), (uint32_t)ESP.getFlashChipSize(),
            homeKitInit ? "true" : "false", deviceInit ? "true" : "false",
            uptime, ESP.getChipModel());
            
        return String(buffer);
    }
}