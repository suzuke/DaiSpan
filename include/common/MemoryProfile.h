#pragma once

#include <Arduino.h>
#include <functional>

namespace MemoryOptimization {

struct ThresholdConfig {
    uint32_t low = 70000;       // 70KB
    uint32_t medium = 55000;    // 55KB
    uint32_t high = 40000;      // 40KB
    uint32_t critical = 30000;  // 30KB
};

struct BufferPoolConfig {
    size_t smallCount = 4;
    size_t mediumCount = 2;
    size_t largeCount = 1;
    bool enableLargePool = true;
};

struct MemoryProfile {
    String name = "c3-baseline";
    String hardwareTag = "ESP32-C3";
    ThresholdConfig thresholds{};
    BufferPoolConfig pools{};
    size_t streamingChunkSize = 512;
    size_t maxRenderSize = 2048;
    bool enableStreaming = true;
    bool enableLargeTemplates = true;
    uint32_t expectedHeapSize = 140000; // bytes
    String selectionReason = "ESP32-C3 profile";
};

const MemoryProfile& GetActiveMemoryProfile();
const MemoryProfile& RefreshActiveMemoryProfile();

String DescribeProfile(const MemoryProfile& profile);

}  // namespace MemoryOptimization
