#include "core/Scheduler.h"

namespace DaiSpan {
namespace Core {

void Scheduler::addTask(TaskPriority priority,
                        const char* name,
                        uint32_t intervalMs,
                        std::function<void()> handler,
                        bool startImmediately) {
    Task task{
        priority,
        name,
        intervalMs,
        startImmediately ? millis() : (millis() + intervalMs),
        std::move(handler),
        true
    };

    switch (priority) {
        case TaskPriority::High:
            highPriorityTasks.push_back(task);
            break;
        case TaskPriority::Medium:
            mediumPriorityTasks.push_back(task);
            break;
        case TaskPriority::Low:
            lowPriorityTasks.push_back(task);
            break;
    }
}

void Scheduler::run(uint32_t now) {
    lastExecutedCount = 0;
    uint32_t startUs = micros();

    runTaskList(highPriorityTasks, now);
    runTaskList(mediumPriorityTasks, now);

    if (lowPriorityTasks.empty()) {
        lastRunDurationUs = micros() - startUs;
        return;
    }

    if (!lowPriorityThrottled) {
        runTaskList(lowPriorityTasks, now);
        lastLowPriorityRun = now;
        lastRunDurationUs = micros() - startUs;
        return;
    }

    if (now - lastLowPriorityRun >= lowPrioritySkipInterval) {
        runTaskList(lowPriorityTasks, now);
        lastLowPriorityRun = now;
    }

    lastRunDurationUs = micros() - startUs;
}

void Scheduler::setLowPriorityThrottled(bool throttled) {
    if (!lowPriorityThrottled && throttled) {
        // entering throttled mode, reset timer so we delay next low-priority run
        lastLowPriorityRun = millis();
    }
    lowPriorityThrottled = throttled;
}

void Scheduler::setLowPrioritySkipInterval(uint32_t intervalMs) {
    lowPrioritySkipInterval = intervalMs;
}

void Scheduler::runTaskList(std::vector<Task>& tasks, uint32_t now) {
    for (auto& task : tasks) {
        if (!task.enabled) {
            continue;
        }
        if (now >= task.nextRun) {
            task.handler();
            task.nextRun = now + task.intervalMs;
            lastExecutedCount++;
        }
    }
}

} // namespace Core
} // namespace DaiSpan
