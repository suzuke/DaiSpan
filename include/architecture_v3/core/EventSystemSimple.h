#pragma once

#include <functional>
#include <unordered_map>
#include <queue>
#include <vector>
#include <memory>
#include <chrono>
#include <type_traits>
#include <string>

namespace DaiSpan::Core {

/**
 * 基礎事件類別（不使用 RTTI）
 */
class DomainEvent {
public:
    virtual ~DomainEvent() = default;
    virtual std::string getEventType() const = 0;
    
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
    uint64_t eventId = generateEventId();

private:
    static uint64_t generateEventId() {
        static uint64_t counter = 0;
        return ++counter;
    }
};

/**
 * 簡化的事件發布器（不使用 RTTI）
 */
class EventPublisher {
public:
    using EventHandler = std::function<void(const DomainEvent&)>;
    
    /**
     * 構造函數，確保所有成員變數正確初始化
     */
    EventPublisher() : nextHandlerId_(1), processedEventCount_(0) {}
    
    /**
     * 訂閱特定類型的事件
     */
    uint32_t subscribe(const std::string& eventType, EventHandler handler) {
        uint32_t handlerId = nextHandlerId_++;
        handlers_[eventType].emplace_back(handlerId, std::move(handler));
        return handlerId;
    }
    
    /**
     * 發布事件
     */
    void publish(std::unique_ptr<DomainEvent> event) {
        if (!event) return;
        
        eventQueue.push(std::move(event));
    }
    
    /**
     * 處理事件隊列
     */
    void processEvents(int maxEvents = 10) {
        int processed = 0;
        while (!eventQueue.empty() && processed < maxEvents) {
            auto event = std::move(eventQueue.front());
            eventQueue.pop();
            
            dispatchEvent(*event);
            processed++;
        }
    }
    
    /**
     * 取消訂閱
     */
    void unsubscribe(const std::string& eventType, uint32_t handlerId) {
        auto it = handlers_.find(eventType);
        if (it != handlers_.end()) {
            auto& handlerList = it->second;
            handlerList.erase(
                std::remove_if(handlerList.begin(), handlerList.end(),
                    [handlerId](const auto& pair) { return pair.first == handlerId; }),
                handlerList.end());
        }
    }
    
    /**
     * 清除所有處理器
     */
    void clear() {
        handlers_.clear();
        while (!eventQueue.empty()) {
            eventQueue.pop();
        }
    }
    
    /**
     * 獲取隊列大小
     */
    size_t getQueueSize() const {
        return eventQueue.size();
    }
    
    /**
     * 獲取訂閱數量
     */
    size_t getSubscriptionCount() const {
        size_t total = 0;
        for (const auto& [eventType, handlers] : handlers_) {
            total += handlers.size();
        }
        return total;
    }
    
    /**
     * 獲取已處理事件數
     */
    size_t getProcessedEventCount() const {
        return processedEventCount_;
    }
    
    /**
     * 重置統計計數器
     */
    void resetStatistics() {
        processedEventCount_ = 0;
    }

private:
    std::unordered_map<std::string, std::vector<std::pair<uint32_t, EventHandler>>> handlers_;
    std::queue<std::unique_ptr<DomainEvent>> eventQueue;
    uint32_t nextHandlerId_ = 1;
    size_t processedEventCount_ = 0;  // 已處理事件計數
    
    void dispatchEvent(const DomainEvent& event) {
        auto it = handlers_.find(event.getEventType());
        if (it != handlers_.end()) {
            bool eventProcessed = false;
            for (const auto& [id, handler] : it->second) {
                try {
                    handler(event);
                    eventProcessed = true;
                } catch (...) {
                    // 忽略處理器中的錯誤，防止影響其他處理器
                }
            }
            if (eventProcessed) {
                processedEventCount_++;  // 每個事件只計算一次
            }
        }
    }
};

/**
 * 簡化的事件處理器基類
 */
class EventHandler {
public:
    explicit EventHandler(EventPublisher& eventBus) : eventBus_(eventBus) {}
    virtual ~EventHandler() = default;

protected:
    EventPublisher& eventBus_;
    std::vector<uint32_t> subscriptions_;
    
    /**
     * 訂閱事件
     */
    uint32_t subscribe(const std::string& eventType, std::function<void(const DomainEvent&)> handler) {
        auto id = eventBus_.subscribe(eventType, std::move(handler));
        subscriptions_.push_back(id);
        return id;
    }
    
    /**
     * 清理訂閱
     */
    void cleanup() {
        for (auto id : subscriptions_) {
            // Note: 這裡需要知道事件類型才能取消訂閱，簡化實作中省略
        }
        subscriptions_.clear();
    }
};

} // namespace DaiSpan::Core