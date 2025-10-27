#include "common/MemoryProfile.h"

namespace MemoryOptimization {

namespace {

MemoryProfile g_activeProfile{};
bool g_profileInitialized = false;

MemoryProfile buildC3Profile(bool productionMode, bool otaEnabled) {
    MemoryProfile profile;
    profile.hardwareTag = "ESP32-C3";
    profile.expectedHeapSize = 140000;  // 約 140KB 可用堆積
    profile.name = productionMode ? (otaEnabled ? "c3-production-ota" : "c3-production")
                                  : (otaEnabled ? "c3-dev-ota" : "c3-dev");
    profile.selectionReason = "ESP32-C3 minimal profile";

    if (productionMode) {
        profile.thresholds = {65000, 52000, 38000, 30000};
        profile.pools = {4, 1, 0, false};
        profile.streamingChunkSize = 384;
        profile.maxRenderSize = 1536;
        profile.enableStreaming = false;  // 生產模式預設停用流式響應
        profile.enableLargeTemplates = false;
    } else {
        profile.thresholds = {70000, 55000, 40000, 30000};
        profile.pools = {4, 2, 1, true};
        profile.streamingChunkSize = otaEnabled ? 512 : 384;
        profile.maxRenderSize = otaEnabled ? 2048 : 1536;
    }

    if (otaEnabled) {
        profile.selectionReason += " (OTA enabled)";
    }

    return profile;
}

MemoryProfile selectProfile() {
    const bool productionMode =
#ifdef PRODUCTION_BUILD
        true;
#else
        false;
#endif
    ;

    const bool otaEnabled =
#ifdef ENABLE_OTA_UPDATE
        true;
#else
        false;
#endif
    ;

    return buildC3Profile(productionMode, otaEnabled);
}

}  // namespace

const MemoryProfile& RefreshActiveMemoryProfile() {
    g_activeProfile = selectProfile();
    g_profileInitialized = true;
    return g_activeProfile;
}

const MemoryProfile& GetActiveMemoryProfile() {
    if (!g_profileInitialized) {
        RefreshActiveMemoryProfile();
    }
    return g_activeProfile;
}

String DescribeProfile(const MemoryProfile& profile) {
    String description;
    description.reserve(192);
    description += "Profile: ";
    description += profile.name;
    description += " (";
    description += profile.hardwareTag;
    description += ")\n";
    description += "Thresholds (low/medium/high/critical): ";
    description += String(profile.thresholds.low);
    description += "/";
    description += String(profile.thresholds.medium);
    description += "/";
    description += String(profile.thresholds.high);
    description += "/";
    description += String(profile.thresholds.critical);
    description += "\nPools (S/M/L): ";
    description += String(profile.pools.smallCount);
    description += "/";
    description += String(profile.pools.mediumCount);
    description += "/";
    description += String(profile.pools.largeCount);
    description += "\nChunk Size: ";
    description += String(profile.streamingChunkSize);
    description += " bytes, Max Render: ";
    description += String(profile.maxRenderSize);
    description += " bytes\nReason: ";
    description += profile.selectionReason;
    return description;
}

}  // namespace MemoryOptimization
