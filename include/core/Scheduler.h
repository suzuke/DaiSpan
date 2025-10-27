#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

namespace DaiSpan {
namespace Core {

enum class TaskPriority {
    High = 0,
    Medium = 1,
    Low = 2,
};

class Scheduler {
public:
    struct Task {
        TaskPriority priority;
        const char* name;
        uint32_t intervalMs;
        uint32_t nextRun;
        std::function<void()> handler;
        bool enabled;
    };

    void addTask(TaskPriority priority,
                 const char* name,
                 uint32_t intervalMs,
                 std::function<void()> handler,
                 bool startImmediately = true);

    void run(uint32_t now);

    void setLowPriorityThrottled(bool throttled);
    void setLowPrioritySkipInterval(uint32_t intervalMs);

    uint16_t getLastExecutedCount() const { return lastExecutedCount; }
    uint32_t getLastRunDurationUs() const { return lastRunDurationUs; }

private:
    std::vector<Task> highPriorityTasks;
    std::vector<Task> mediumPriorityTasks;
    std::vector<Task> lowPriorityTasks;

    bool lowPriorityThrottled = false;
    uint32_t lowPrioritySkipInterval = 200; // default skip window when throttled
    uint32_t lastLowPriorityRun = 0;

    uint16_t lastExecutedCount = 0;
    uint32_t lastRunDurationUs = 0;

    void runTaskList(std::vector<Task>& tasks, uint32_t now);
};

} // namespace Core
} // namespace DaiSpan
