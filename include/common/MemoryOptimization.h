#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <memory>
#include <map>
#include <functional>
#include <mutex>
#include <vector>
#include <numeric>
#include <algorithm>

#include "MemoryProfile.h"

namespace MemoryOptimization {

// ================================================================================================
// 流式响应构建器 - 解决大型缓冲区问题
// ================================================================================================

class StreamingResponseBuilder {
private:
    size_t chunk_size = 512;
    std::vector<char> chunk_buffer;
    size_t buffer_pos = 0;
    WebServer* server = nullptr;
    bool response_started = false;
    bool response_finished = false;

    void ensureBufferAllocated(size_t desiredSize) {
        if (desiredSize == 0) {
            desiredSize = 512;
        }
        if (desiredSize < 256) {
            desiredSize = 256;
        }
        if (chunk_buffer.empty() || chunk_size != desiredSize) {
            chunk_size = desiredSize;
            chunk_buffer.assign(chunk_size + 1, '\0');
            buffer_pos = 0;
        }
    }

public:
    StreamingResponseBuilder() {
        ensureBufferAllocated(GetActiveMemoryProfile().streamingChunkSize);
    }

    explicit StreamingResponseBuilder(size_t configuredChunkSize) {
        ensureBufferAllocated(configuredChunkSize);
    }

    void setChunkSize(size_t newChunkSize) {
        ensureBufferAllocated(newChunkSize);
    }

    // 开始流式响应
    void begin(WebServer* srv, const String& content_type = "text/html", size_t overrideChunkSize = 0) {
        if (overrideChunkSize > 0) {
            ensureBufferAllocated(overrideChunkSize);
        }
        server = srv;
        server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        server->send(200, content_type, "");
        response_started = true;
        response_finished = false;
        buffer_pos = 0;
        if (!chunk_buffer.empty()) {
            chunk_buffer[0] = '\0';
        }
    }

    // 添加内容到缓冲区
    void append(const char* content) {
        if (!response_started || response_finished) return;

        size_t content_len = strlen(content);
        size_t pos = 0;

        while (pos < content_len) {
            size_t copy_len = min(content_len - pos, chunk_size - buffer_pos);
            memcpy(chunk_buffer.data() + buffer_pos, content + pos, copy_len);
            buffer_pos += copy_len;
            pos += copy_len;

            // 如果缓冲区满了，发送chunk
            if (buffer_pos >= chunk_size) {
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
        server->sendContent(chunk_buffer.data());
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
    size_t getBufferCapacity() const { return chunk_size; }
    size_t getChunkSize() const { return chunk_size; }
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
        
        BufferGuard(const BufferGuard&) = delete;
        BufferGuard& operator=(const BufferGuard&) = delete;
        
        BufferGuard(BufferGuard&& other) noexcept 
            : pool(other.pool), buffer(other.buffer), size(other.size), index(other.index) {
            other.pool = nullptr;
            other.buffer = nullptr;
        }
        
        BufferGuard& operator=(BufferGuard&& other) noexcept {
            if (this != &other) {
                if (pool && buffer) {
                    pool->returnBuffer(buffer, size, index);
                }
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

    BufferPool() {
        applyProfile(GetActiveMemoryProfile());
    }

    explicit BufferPool(const MemoryProfile& profileRef) {
        applyProfile(profileRef);
    }

    void applyProfile(const MemoryProfile& profileRef) {
        std::lock_guard<std::mutex> lock(pool_mutex);
        profile = &profileRef;
        pool_config = profileRef.pools;

        auto initPool = [](std::vector<std::unique_ptr<char[]>>& buffers,
                           std::vector<uint8_t>& used,
                           size_t count,
                           BufferSize size) {
            buffers.clear();
            used.clear();
            buffers.reserve(count);
            used.resize(count, 0);
            for (size_t i = 0; i < count; ++i) {
                buffers.emplace_back(std::make_unique<char[]>(static_cast<size_t>(size)));
            }
        };

        initPool(small_buffers, small_used, pool_config.smallCount, BufferSize::SMALL);
        initPool(medium_buffers, medium_used, pool_config.mediumCount, BufferSize::MEDIUM);

        if (pool_config.enableLargePool && pool_config.largeCount > 0) {
            initPool(large_buffers, large_used, pool_config.largeCount, BufferSize::LARGE);
        } else {
            large_buffers.clear();
            large_used.clear();
        }

        stats = {};
    }

    const BufferPoolConfig& getConfig() const {
        return pool_config;
    }

    bool hasLargeBuffers() const {
        return pool_config.enableLargePool && pool_config.largeCount > 0;
    }

    std::unique_ptr<BufferGuard> acquireBuffer(BufferSize size) {
        std::lock_guard<std::mutex> lock(pool_mutex);
        stats.total_requests++;

        auto tryAcquire = [&](std::vector<std::unique_ptr<char[]>>& buffers,
                              std::vector<uint8_t>& usedFlags,
                              BufferSize bufferSize,
                              uint32_t& allocationCounter) -> std::unique_ptr<BufferGuard> {
            for (size_t i = 0; i < usedFlags.size(); ++i) {
                if (!usedFlags[i]) {
                    usedFlags[i] = 1;
                    stats.successful_allocations++;
                    allocationCounter++;
                    return std::make_unique<BufferGuard>(this, buffers[i].get(), bufferSize, i);
                }
            }
            return nullptr;
        };

        std::unique_ptr<BufferGuard> guard;
        BufferSize currentSize = size;
        for (int attempt = 0; attempt < 3 && !guard; ++attempt) {
            switch (currentSize) {
                case BufferSize::SMALL:
                    guard = tryAcquire(small_buffers, small_used, BufferSize::SMALL, stats.small_allocations);
                    break;
                case BufferSize::MEDIUM:
                    guard = tryAcquire(medium_buffers, medium_used, BufferSize::MEDIUM, stats.medium_allocations);
                    if (!guard) {
                        currentSize = BufferSize::SMALL;
                        continue;
                    }
                    break;
                case BufferSize::LARGE:
                    if (!large_buffers.empty()) {
                        guard = tryAcquire(large_buffers, large_used, BufferSize::LARGE, stats.large_allocations);
                        if (guard) {
                            break;
                        }
                    }
                    currentSize = BufferSize::MEDIUM;
                    continue;
            }
        }

        if (!guard) {
            stats.failed_allocations++;
        }

        return guard;
    }

    void getStats(String& stats_str) const {
        std::lock_guard<std::mutex> lock(pool_mutex);
        stats_str = "BufferPool Stats:\n";
        stats_str += "Profile: ";
        stats_str += profile ? profile->name : "unknown";
        stats_str += "\nTotal Requests: " + String(stats.total_requests) + "\n";
        stats_str += "Successful: " + String(stats.successful_allocations) + "\n";
        stats_str += "Failed: " + String(stats.failed_allocations) + "\n";
        stats_str += "Small/Medium/Large: " + String(stats.small_allocations) + "/" + 
                     String(stats.medium_allocations) + "/" + String(stats.large_allocations) + "\n";
        auto countUsage = [](const std::vector<uint8_t>& data) {
            return std::accumulate(data.begin(), data.end(), 0);
        };
        stats_str += "Current Usage: " + String(countUsage(small_used)) + "/" + String(pool_config.smallCount) + " (S), " +
                     String(countUsage(medium_used)) + "/" + String(pool_config.mediumCount) + " (M), " +
                     String(countUsage(large_used)) + "/" + String(pool_config.largeCount) + " (L)\n";
    }

private:
    const MemoryProfile* profile = nullptr;
    BufferPoolConfig pool_config{};

    std::vector<std::unique_ptr<char[]>> small_buffers;
    std::vector<std::unique_ptr<char[]>> medium_buffers;
    std::vector<std::unique_ptr<char[]>> large_buffers;

    std::vector<uint8_t> small_used;
    std::vector<uint8_t> medium_used;
    std::vector<uint8_t> large_used;

    mutable std::mutex pool_mutex;

    struct {
        uint32_t total_requests = 0;
        uint32_t successful_allocations = 0;
        uint32_t failed_allocations = 0;
        uint32_t small_allocations = 0;
        uint32_t medium_allocations = 0;
        uint32_t large_allocations = 0;
    } stats;

    friend class BufferGuard;

    void returnBuffer(char* buffer, BufferSize size, size_t index) {
        if (!buffer) {
            return;
        }

        std::lock_guard<std::mutex> lock(pool_mutex);

        auto release = [&](std::vector<uint8_t>& usedFlags) {
            if (index < usedFlags.size()) {
                usedFlags[index] = 0;
            }
        };

        switch (size) {
            case BufferSize::SMALL:
                release(small_used);
                break;
            case BufferSize::MEDIUM:
                release(medium_used);
                break;
            case BufferSize::LARGE:
                release(large_used);
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

    const MemoryProfile* profile = nullptr;
    ThresholdConfig thresholds{};
    size_t profile_max_render_size = 2048;

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
    MemoryManager() {
        applyProfile(GetActiveMemoryProfile());
    }

    explicit MemoryManager(const MemoryProfile& profileRef) {
        applyProfile(profileRef);
    }

    void applyProfile(const MemoryProfile& profileRef) {
        profile = &profileRef;
        thresholds = profileRef.thresholds;
        profile_max_render_size = profileRef.maxRenderSize;
        current_pressure = MemoryPressure::PRESSURE_LOW;
        current_strategy = RenderStrategy::FULL_FEATURED;
        last_memory_check = 0;
        consecutive_low_memory = 0;
        stats = {};
    }

    const MemoryProfile& getActiveProfile() const {
        return profile ? *profile : GetActiveMemoryProfile();
    }

    MemoryPressure updateMemoryPressure() {
        uint32_t current_time = millis();
        
        if (current_time - last_memory_check >= MEMORY_CHECK_INTERVAL) {
            uint32_t free_heap = ESP.getFreeHeap();
            
            updateStats(free_heap);
            
            MemoryPressure new_pressure;
            if (free_heap >= thresholds.low) {
                new_pressure = MemoryPressure::PRESSURE_LOW;
                consecutive_low_memory = 0;
            } else if (free_heap >= thresholds.medium) {
                new_pressure = MemoryPressure::PRESSURE_MEDIUM;
                consecutive_low_memory++;
            } else if (free_heap >= thresholds.high) {
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

    bool shouldUseStreamingResponse() {
        updateMemoryPressure();
        if (profile && !profile->enableStreaming) {
            return false;
        }
        return current_strategy == RenderStrategy::FULL_FEATURED || 
               current_strategy == RenderStrategy::OPTIMIZED;
    }

    size_t getMaxBufferSize() const {
        size_t base = profile_max_render_size;
        switch (current_strategy) {
            case RenderStrategy::FULL_FEATURED:
                return base;
            case RenderStrategy::OPTIMIZED:
                return base > 512 ? std::max<size_t>(base * 3 / 4, 768) : base;
            case RenderStrategy::MINIMAL:
                return base > 512 ? std::max<size_t>(base / 2, static_cast<size_t>(512)) : base;
            case RenderStrategy::EMERGENCY:
                return std::max<size_t>(base / 4, static_cast<size_t>(256));
            default:
                return std::max<size_t>(base / 2, static_cast<size_t>(512));
        }
    }

    BufferPool::BufferSize getRecommendedBufferSize() const {
        size_t max_size = getMaxBufferSize();
        const bool hasLarge = profile ? (profile->pools.enableLargePool && profile->pools.largeCount > 0) : true;
        const bool hasMedium = profile ? (profile->pools.mediumCount > 0) : true;
        if (hasLarge && max_size >= static_cast<size_t>(BufferPool::BufferSize::LARGE)) {
            return BufferPool::BufferSize::LARGE;
        }
        if (hasMedium && max_size >= static_cast<size_t>(BufferPool::BufferSize::MEDIUM)) {
            return BufferPool::BufferSize::MEDIUM;
        }
        return BufferPool::BufferSize::SMALL;
    }

    bool shouldServePage(const String& page_type) {
        updateMemoryPressure();
        if (current_strategy == RenderStrategy::EMERGENCY) {
            return page_type == "status" || page_type == "error";
        }
        if (profile && !profile->enableLargeTemplates && 
            (current_strategy == RenderStrategy::MINIMAL) &&
            page_type != "status" && page_type != "error") {
            return false;
        }
        return true;
    }

    bool isEmergencyMode() {
        updateMemoryPressure();
        return current_strategy == RenderStrategy::EMERGENCY;
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
        if (profile) {
            stats_str += "Active Profile: " + profile->name + " (" + profile->hardwareTag + ")\n";
            stats_str += "Thresholds: low=" + String(thresholds.low) + 
                         ", medium=" + String(thresholds.medium) +
                         ", high=" + String(thresholds.high) +
                         ", critical=" + String(thresholds.critical) + "\n";
            stats_str += "Streaming Chunk: " + String(profile->streamingChunkSize) + " bytes\n";
            stats_str += "Max Render Size: " + String(profile_max_render_size) + " bytes\n";
        }
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
    const MemoryProfile* active_profile = nullptr;
    std::unique_ptr<MemoryManager> memory_manager;
    std::unique_ptr<BufferPool> buffer_pool;
    std::unique_ptr<StreamingResponseBuilder> stream_builder;

public:
    WebPageGenerator() {
        const auto& profile = GetActiveMemoryProfile();
        memory_manager = std::make_unique<MemoryManager>(profile);
        buffer_pool = std::make_unique<BufferPool>(profile);
        stream_builder = std::make_unique<StreamingResponseBuilder>(profile.streamingChunkSize);
        active_profile = &profile;
    }

    explicit WebPageGenerator(const MemoryProfile& profile) {
        memory_manager = std::make_unique<MemoryManager>(profile);
        buffer_pool = std::make_unique<BufferPool>(profile);
        stream_builder = std::make_unique<StreamingResponseBuilder>(profile.streamingChunkSize);
        active_profile = &profile;
    }

    void applyProfile(const MemoryProfile& profile) {
        active_profile = &profile;
        if (memory_manager) {
            memory_manager->applyProfile(profile);
        }
        if (buffer_pool) {
            buffer_pool->applyProfile(profile);
        }
        if (stream_builder) {
            stream_builder->setChunkSize(profile.streamingChunkSize);
        }
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
        stream_builder->begin(server, "text/html", active_profile ? active_profile->streamingChunkSize : 0);
        
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
            server->send(200, "text/html", 
                        "<html><body><h1>系统内存不足</h1><p>当前处于紧急模式，仅保留核心功能。</p><p><a href='/' style='display:inline-block;margin-top:10px;'>返回主頁</a></p></body></html>");
            return;
        }

        auto strategy = memory_manager->getRenderStrategy();
        if (strategy == MemoryManager::RenderStrategy::EMERGENCY) {
            server->send(200, "text/html", 
                        "<html><body><h1>系统内存不足</h1><p>当前无法加载完整 WiFi 配置頁面。</p><p><a href='/' style='display:inline-block;margin-top:10px;'>返回主頁</a></p></body></html>");
            return;
        }

        stream_builder->begin(server, "text/html", active_profile ? active_profile->streamingChunkSize : 0);
        
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
        if (active_profile) {
            stats += "Active Profile: " + active_profile->name + " (" + active_profile->hardwareTag + ")\n";
            stats += "Profile Chunk Size: " + String(active_profile->streamingChunkSize) + " bytes\n";
            stats += "Profile Reason: " + active_profile->selectionReason + "\n";
        }
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
