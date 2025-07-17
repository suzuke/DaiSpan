#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <memory>
#include <map>
#include <functional>
#include <bitset>
#include <mutex>

namespace MemoryOptimization {

// ================================================================================================
// 流式响应构建器 - 解决大型缓冲区问题
// ================================================================================================

class StreamingResponseBuilder {
private:
    static constexpr size_t CHUNK_SIZE = 512;
    char chunk_buffer[CHUNK_SIZE + 1]; // +1 for null terminator
    size_t buffer_pos = 0;
    WebServer* server = nullptr;
    bool response_started = false;
    bool response_finished = false;

public:
    StreamingResponseBuilder() {
        chunk_buffer[0] = '\0';
    }

    // 开始流式响应
    void begin(WebServer* srv, const String& content_type = "text/html") {
        server = srv;
        server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        server->send(200, content_type, "");
        response_started = true;
        response_finished = false;
        buffer_pos = 0;
        chunk_buffer[0] = '\0';
    }

    // 添加内容到缓冲区
    void append(const char* content) {
        if (!response_started || response_finished) return;

        size_t content_len = strlen(content);
        size_t pos = 0;

        while (pos < content_len) {
            size_t copy_len = min(content_len - pos, CHUNK_SIZE - buffer_pos);
            memcpy(chunk_buffer + buffer_pos, content + pos, copy_len);
            buffer_pos += copy_len;
            pos += copy_len;

            // 如果缓冲区满了，发送chunk
            if (buffer_pos >= CHUNK_SIZE) {
                flush();
            }
        }
    }

    // 格式化添加内容
    void appendf(const char* format, ...) {
        char temp_buffer[256];
        va_list args;
        va_start(args, format);
        int len = vsnprintf(temp_buffer, sizeof(temp_buffer), format, args);
        va_end(args);

        if (len > 0) {
            append(temp_buffer);
        }
    }

    // 发送当前缓冲区内容
    void flush() {
        if (!response_started || response_finished || buffer_pos == 0) return;

        chunk_buffer[buffer_pos] = '\0';
        server->sendContent(chunk_buffer);
        buffer_pos = 0;
        chunk_buffer[0] = '\0';
    }

    // 完成响应
    void finish() {
        if (!response_started || response_finished) return;

        flush(); // 发送剩余内容
        server->sendContent(""); // 发送空内容表示结束
        response_finished = true;
    }

    // 获取当前缓冲区使用情况
    size_t getBufferUsage() const { return buffer_pos; }
    size_t getBufferCapacity() const { return CHUNK_SIZE; }
};

// ================================================================================================
// 缓冲区池化管理 - 解决动态内存分配问题
// ================================================================================================

class BufferPool {
public:
    enum class BufferSize : size_t {
        SMALL = 512,
        MEDIUM = 1024,
        LARGE = 2048
    };

    class BufferGuard {
    private:
        BufferPool* pool;
        char* buffer;
        BufferSize size;
        size_t index;
        
    public:
        BufferGuard(BufferPool* p, char* buf, BufferSize sz, size_t idx) 
            : pool(p), buffer(buf), size(sz), index(idx) {}
        
        ~BufferGuard() {
            if (pool && buffer) {
                pool->returnBuffer(buffer, size, index);
            }
        }
        
        // 禁止复制
        BufferGuard(const BufferGuard&) = delete;
        BufferGuard& operator=(const BufferGuard&) = delete;
        
        // 支持移动
        BufferGuard(BufferGuard&& other) noexcept 
            : pool(other.pool), buffer(other.buffer), size(other.size), index(other.index) {
            other.pool = nullptr;
            other.buffer = nullptr;
        }
        
        BufferGuard& operator=(BufferGuard&& other) noexcept {
            if (this != &other) {
                // 释放当前资源
                if (pool && buffer) {
                    pool->returnBuffer(buffer, size, index);
                }
                
                // 移动资源
                pool = other.pool;
                buffer = other.buffer;
                size = other.size;
                index = other.index;
                
                other.pool = nullptr;
                other.buffer = nullptr;
            }
            return *this;
        }
        
        char* get() { return buffer; }
        size_t capacity() const { return static_cast<size_t>(size); }
        bool isValid() const { return buffer != nullptr; }
    };

private:
    // 不同大小的缓冲区池
    static constexpr size_t SMALL_POOL_SIZE = 4;
    static constexpr size_t MEDIUM_POOL_SIZE = 2;
    static constexpr size_t LARGE_POOL_SIZE = 1;

    std::array<std::unique_ptr<char[]>, SMALL_POOL_SIZE> small_buffers;
    std::array<std::unique_ptr<char[]>, MEDIUM_POOL_SIZE> medium_buffers;
    std::array<std::unique_ptr<char[]>, LARGE_POOL_SIZE> large_buffers;

    std::bitset<SMALL_POOL_SIZE> small_used;
    std::bitset<MEDIUM_POOL_SIZE> medium_used;
    std::bitset<LARGE_POOL_SIZE> large_used;

    mutable std::mutex pool_mutex;

    // 统计信息
    struct {
        uint32_t total_requests = 0;
        uint32_t successful_allocations = 0;
        uint32_t failed_allocations = 0;
        uint32_t small_allocations = 0;
        uint32_t medium_allocations = 0;
        uint32_t large_allocations = 0;
    } stats;

public:
    BufferPool() {
        // 预分配所有缓冲区
        for (size_t i = 0; i < SMALL_POOL_SIZE; ++i) {
            small_buffers[i] = std::make_unique<char[]>(static_cast<size_t>(BufferSize::SMALL));
        }
        for (size_t i = 0; i < MEDIUM_POOL_SIZE; ++i) {
            medium_buffers[i] = std::make_unique<char[]>(static_cast<size_t>(BufferSize::MEDIUM));
        }
        for (size_t i = 0; i < LARGE_POOL_SIZE; ++i) {
            large_buffers[i] = std::make_unique<char[]>(static_cast<size_t>(BufferSize::LARGE));
        }
    }

    std::unique_ptr<BufferGuard> acquireBuffer(BufferSize size) {
        std::lock_guard<std::mutex> lock(pool_mutex);
        stats.total_requests++;

        switch (size) {
            case BufferSize::SMALL:
                for (size_t i = 0; i < SMALL_POOL_SIZE; ++i) {
                    if (!small_used[i]) {
                        small_used[i] = true;
                        stats.successful_allocations++;
                        stats.small_allocations++;
                        return std::make_unique<BufferGuard>(this, small_buffers[i].get(), size, i);
                    }
                }
                break;
                
            case BufferSize::MEDIUM:
                for (size_t i = 0; i < MEDIUM_POOL_SIZE; ++i) {
                    if (!medium_used[i]) {
                        medium_used[i] = true;
                        stats.successful_allocations++;
                        stats.medium_allocations++;
                        return std::make_unique<BufferGuard>(this, medium_buffers[i].get(), size, i);
                    }
                }
                break;
                
            case BufferSize::LARGE:
                for (size_t i = 0; i < LARGE_POOL_SIZE; ++i) {
                    if (!large_used[i]) {
                        large_used[i] = true;
                        stats.successful_allocations++;
                        stats.large_allocations++;
                        return std::make_unique<BufferGuard>(this, large_buffers[i].get(), size, i);
                    }
                }
                break;
        }

        stats.failed_allocations++;
        return nullptr;
    }

    // 获取池使用统计
    void getStats(String& stats_str) const {
        std::lock_guard<std::mutex> lock(pool_mutex);
        stats_str = "BufferPool Stats:\n";
        stats_str += "Total Requests: " + String(stats.total_requests) + "\n";
        stats_str += "Successful: " + String(stats.successful_allocations) + "\n";
        stats_str += "Failed: " + String(stats.failed_allocations) + "\n";
        stats_str += "Small/Medium/Large: " + String(stats.small_allocations) + "/" + 
                     String(stats.medium_allocations) + "/" + String(stats.large_allocations) + "\n";
        stats_str += "Current Usage: " + String(small_used.count()) + "/" + String(SMALL_POOL_SIZE) + " (S), " +
                     String(medium_used.count()) + "/" + String(MEDIUM_POOL_SIZE) + " (M), " +
                     String(large_used.count()) + "/" + String(LARGE_POOL_SIZE) + " (L)\n";
    }

private:
    friend class BufferGuard;
    
    void returnBuffer(char* buffer, BufferSize size, size_t index) {
        std::lock_guard<std::mutex> lock(pool_mutex);
        
        switch (size) {
            case BufferSize::SMALL:
                if (index < SMALL_POOL_SIZE) {
                    small_used[index] = false;
                }
                break;
            case BufferSize::MEDIUM:
                if (index < MEDIUM_POOL_SIZE) {
                    medium_used[index] = false;
                }
                break;
            case BufferSize::LARGE:
                if (index < LARGE_POOL_SIZE) {
                    large_used[index] = false;
                }
                break;
        }
    }
};

// ================================================================================================
// 内存监控管理器 - 实现自适应内存管理
// ================================================================================================

class MemoryManager {
public:
    enum class MemoryPressure {
        PRESSURE_LOW,      // >80KB free heap
        PRESSURE_MEDIUM,   // 50-80KB free heap  
        PRESSURE_HIGH,     // 30-50KB free heap
        PRESSURE_CRITICAL  // <30KB free heap
    };

    enum class RenderStrategy {
        FULL_FEATURED,    // 完整功能模式
        OPTIMIZED,        // 优化模式
        MINIMAL,          // 最小化模式
        EMERGENCY         // 紧急模式
    };

private:
    static constexpr uint32_t MEMORY_CHECK_INTERVAL = 5000; // 5秒检查一次
    static constexpr uint32_t LOW_MEMORY_THRESHOLD = 80000;  // 80KB
    static constexpr uint32_t MEDIUM_MEMORY_THRESHOLD = 50000; // 50KB
    static constexpr uint32_t HIGH_MEMORY_THRESHOLD = 30000;   // 30KB

    MemoryPressure current_pressure = MemoryPressure::PRESSURE_LOW;
    RenderStrategy current_strategy = RenderStrategy::FULL_FEATURED;
    
    uint32_t last_memory_check = 0;
    uint32_t consecutive_low_memory = 0;
    
    struct MemoryStats {
        uint32_t min_free_heap = UINT32_MAX;
        uint32_t max_free_heap = 0;
        uint32_t avg_free_heap = 0;
        uint32_t sample_count = 0;
        uint32_t pressure_changes = 0;
        uint32_t emergency_activations = 0;
    } stats;

public:
    MemoryPressure updateMemoryPressure() {
        uint32_t current_time = millis();
        
        if (current_time - last_memory_check >= MEMORY_CHECK_INTERVAL) {
            uint32_t free_heap = ESP.getFreeHeap();
            
            updateStats(free_heap);
            
            MemoryPressure new_pressure;
            if (free_heap >= LOW_MEMORY_THRESHOLD) {
                new_pressure = MemoryPressure::PRESSURE_LOW;
                consecutive_low_memory = 0;
            } else if (free_heap >= MEDIUM_MEMORY_THRESHOLD) {
                new_pressure = MemoryPressure::PRESSURE_MEDIUM;
                consecutive_low_memory++;
            } else if (free_heap >= HIGH_MEMORY_THRESHOLD) {
                new_pressure = MemoryPressure::PRESSURE_HIGH;
                consecutive_low_memory++;
            } else {
                new_pressure = MemoryPressure::PRESSURE_CRITICAL;
                consecutive_low_memory++;
            }
            
            if (new_pressure != current_pressure) {
                current_pressure = new_pressure;
                stats.pressure_changes++;
                updateRenderStrategy();
            }
            
            last_memory_check = current_time;
        }
        
        return current_pressure;
    }

    RenderStrategy getRenderStrategy() {
        updateMemoryPressure();
        return current_strategy;
    }

    bool shouldUseStreamingResponse() const {
        return current_strategy == RenderStrategy::FULL_FEATURED || 
               current_strategy == RenderStrategy::OPTIMIZED;
    }

    size_t getMaxBufferSize() const {
        switch (current_strategy) {
            case RenderStrategy::FULL_FEATURED:
                return 2048;
            case RenderStrategy::OPTIMIZED:
                return 1024;
            case RenderStrategy::MINIMAL:
                return 512;
            case RenderStrategy::EMERGENCY:
                return 256;
            default:
                return 512;
        }
    }

    BufferPool::BufferSize getRecommendedBufferSize() const {
        size_t max_size = getMaxBufferSize();
        if (max_size >= 2048) return BufferPool::BufferSize::LARGE;
        if (max_size >= 1024) return BufferPool::BufferSize::MEDIUM;
        return BufferPool::BufferSize::SMALL;
    }

    bool shouldServePage(const String& page_type) const {
        if (current_strategy == RenderStrategy::EMERGENCY) {
            return page_type == "status" || page_type == "error";
        }
        return true;
    }

    void getMemoryStats(String& stats_str) const {
        stats_str = "Memory Stats:\n";
        stats_str += "Current Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
        stats_str += "Pressure Level: " + String(getPressureName(current_pressure)) + "\n";
        stats_str += "Render Strategy: " + String(getStrategyName(current_strategy)) + "\n";
        stats_str += "Min/Max/Avg Heap: " + String(stats.min_free_heap) + "/" + 
                     String(stats.max_free_heap) + "/" + String(stats.avg_free_heap) + " bytes\n";
        stats_str += "Pressure Changes: " + String(stats.pressure_changes) + "\n";
        stats_str += "Emergency Activations: " + String(stats.emergency_activations) + "\n";
        stats_str += "Sample Count: " + String(stats.sample_count) + "\n";
    }

private:
    void updateStats(uint32_t free_heap) {
        stats.min_free_heap = min(stats.min_free_heap, free_heap);
        stats.max_free_heap = max(stats.max_free_heap, free_heap);
        
        if (stats.sample_count > 0) {
            stats.avg_free_heap = (stats.avg_free_heap * stats.sample_count + free_heap) / (stats.sample_count + 1);
        } else {
            stats.avg_free_heap = free_heap;
        }
        
        stats.sample_count++;
    }

    void updateRenderStrategy() {
        switch (current_pressure) {
            case MemoryPressure::PRESSURE_LOW:
                current_strategy = RenderStrategy::FULL_FEATURED;
                break;
            case MemoryPressure::PRESSURE_MEDIUM:
                current_strategy = RenderStrategy::OPTIMIZED;
                break;
            case MemoryPressure::PRESSURE_HIGH:
                current_strategy = RenderStrategy::MINIMAL;
                break;
            case MemoryPressure::PRESSURE_CRITICAL:
                current_strategy = RenderStrategy::EMERGENCY;
                stats.emergency_activations++;
                triggerGarbageCollection();
                break;
        }
    }

    void triggerGarbageCollection() {
        // 触发垃圾回收 - ESP32没有显式垃圾回收，这里只是记录
        // ESP32的内存管理是自动的，我们只能监控而不能强制回收
    }

    const char* getPressureName(MemoryPressure pressure) const {
        switch (pressure) {
            case MemoryPressure::PRESSURE_LOW: return "LOW";
            case MemoryPressure::PRESSURE_MEDIUM: return "MEDIUM";
            case MemoryPressure::PRESSURE_HIGH: return "HIGH";
            case MemoryPressure::PRESSURE_CRITICAL: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }

    const char* getStrategyName(RenderStrategy strategy) const {
        switch (strategy) {
            case RenderStrategy::FULL_FEATURED: return "FULL_FEATURED";
            case RenderStrategy::OPTIMIZED: return "OPTIMIZED";
            case RenderStrategy::MINIMAL: return "MINIMAL";
            case RenderStrategy::EMERGENCY: return "EMERGENCY";
            default: return "UNKNOWN";
        }
    }
};

// ================================================================================================
// 统一的Web页面生成器 - 整合所有优化组件
// ================================================================================================

class WebPageGenerator {
private:
    std::unique_ptr<MemoryManager> memory_manager;
    std::unique_ptr<BufferPool> buffer_pool;
    std::unique_ptr<StreamingResponseBuilder> stream_builder;

public:
    WebPageGenerator() {
        memory_manager = std::make_unique<MemoryManager>();
        buffer_pool = std::make_unique<BufferPool>();
        stream_builder = std::make_unique<StreamingResponseBuilder>();
    }

    // 生成简单的状态页面
    void generateStatusPage(WebServer* server) {
        if (!memory_manager->shouldServePage("status")) {
            server->send(503, "text/html", 
                        "<html><body><h1>系统内存不足</h1><p>请稍后重试</p></body></html>");
            return;
        }

        auto strategy = memory_manager->getRenderStrategy();
        
        if (strategy == MemoryManager::RenderStrategy::EMERGENCY) {
            // 紧急模式：最小化HTML
            server->send(200, "text/html", 
                        "<html><body><h1>系统状态</h1><p>内存: " + String(ESP.getFreeHeap()) + " bytes</p></body></html>");
            return;
        }

        // 使用流式响应
        stream_builder->begin(server);
        
        // 生成HTML头部
        stream_builder->append("<!DOCTYPE html><html><head>");
        stream_builder->append("<title>系统状态</title>");
        stream_builder->append("<meta charset='utf-8'>");
        stream_builder->append("<style>body{font-family:Arial,sans-serif;margin:20px;}</style>");
        stream_builder->append("</head><body>");
        
        // 生成内容
        stream_builder->append("<h1>🔧 系统状态</h1>");
        
        // 内存信息
        generateMemoryInfo();
        
        // 缓冲区池信息
        generateBufferPoolInfo();
        
        // 结束HTML
        stream_builder->append("</body></html>");
        stream_builder->finish();
    }

    // 生成WiFi配置页面（简化版）
    void generateWiFiConfigPage(WebServer* server) {
        if (!memory_manager->shouldServePage("wifi_config")) {
            server->send(503, "text/html", 
                        "<html><body><h1>系统内存不足</h1><p>请稍后重试</p></body></html>");
            return;
        }

        stream_builder->begin(server);
        
        // HTML头部
        stream_builder->append("<!DOCTYPE html><html><head>");
        stream_builder->append("<title>WiFi配置</title>");
        stream_builder->append("<meta charset='utf-8'>");
        stream_builder->append("<style>");
        stream_builder->append("body{font-family:Arial,sans-serif;margin:20px;}");
        stream_builder->append(".container{max-width:500px;margin:0 auto;}");
        stream_builder->append(".form-group{margin-bottom:15px;}");
        stream_builder->append("label{display:block;margin-bottom:5px;font-weight:bold;}");
        stream_builder->append("input,select{width:100%;padding:8px;border:1px solid #ccc;border-radius:4px;}");
        stream_builder->append("button{background:#007bff;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;}");
        stream_builder->append("</style>");
        stream_builder->append("</head><body>");
        
        // 内容
        stream_builder->append("<div class='container'>");
        stream_builder->append("<h1>📡 WiFi配置</h1>");
        stream_builder->append("<form method='post' action='/save'>");
        
        // WiFi网络选择
        generateWiFiNetworkOptions();
        
        // 密码输入
        stream_builder->append("<div class='form-group'>");
        stream_builder->append("<label>密码:</label>");
        stream_builder->append("<input type='password' name='password' required>");
        stream_builder->append("</div>");
        
        // 提交按钮
        stream_builder->append("<button type='submit'>保存配置</button>");
        stream_builder->append("</form>");
        
        // 返回主頁按鈕
        stream_builder->append("<div style='margin-top: 20px; text-align: center;'>");
        stream_builder->append("<a href='/' style='background:#6c757d;color:white;padding:10px 20px;text-decoration:none;border-radius:4px;'>返回主頁</a>");
        stream_builder->append("</div>");
        
        stream_builder->append("</div></body></html>");
        
        stream_builder->finish();
    }

    // 获取系统统计信息
    void getSystemStats(String& stats) {
        String memory_stats, buffer_stats;
        memory_manager->getMemoryStats(memory_stats);
        buffer_pool->getStats(buffer_stats);
        
        stats = "=== 系统统计信息 ===\n";
        stats += memory_stats + "\n";
        stats += buffer_stats + "\n";
        stats += "Stream Buffer Usage: " + String(stream_builder->getBufferUsage()) + "/" + 
                 String(stream_builder->getBufferCapacity()) + " bytes\n";
    }

private:
    void generateMemoryInfo() {
        String memory_stats;
        memory_manager->getMemoryStats(memory_stats);
        
        stream_builder->append("<div><h2>内存状态</h2><pre>");
        stream_builder->append(memory_stats.c_str());
        stream_builder->append("</pre></div>");
    }

    void generateBufferPoolInfo() {
        String buffer_stats;
        buffer_pool->getStats(buffer_stats);
        
        stream_builder->append("<div><h2>缓冲区池状态</h2><pre>");
        stream_builder->append(buffer_stats.c_str());
        stream_builder->append("</pre></div>");
    }

    void generateWiFiNetworkOptions() {
        stream_builder->append("<div class='form-group'>");
        stream_builder->append("<label>网络名称:</label>");
        stream_builder->append("<select name='ssid'>");
        
        // 获取缓冲区来生成WiFi选项
        auto buffer_guard = buffer_pool->acquireBuffer(memory_manager->getRecommendedBufferSize());
        if (buffer_guard && buffer_guard->isValid()) {
            char* buffer = buffer_guard->get();
            size_t buffer_size = buffer_guard->capacity();
            
            int networks = WiFi.scanNetworks();
            size_t pos = 0;
            
            for (int i = 0; i < networks && pos < buffer_size - 100; i++) {
                int written = snprintf(buffer + pos, buffer_size - pos,
                                     "<option value=\"%s\">%s (%d dBm)</option>",
                                     WiFi.SSID(i).c_str(), WiFi.SSID(i).c_str(), WiFi.RSSI(i));
                if (written > 0) {
                    pos += written;
                }
            }
            
            buffer[pos] = '\0';
            stream_builder->append(buffer);
        } else {
            stream_builder->append("<option>内存不足，无法扫描网络</option>");
        }
        
        stream_builder->append("</select></div>");
    }
};

} // namespace MemoryOptimization