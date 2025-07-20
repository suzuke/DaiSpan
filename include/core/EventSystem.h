#pragma once

#include <functional>
#include <unordered_map>
#include <queue>
#include <vector>
#include <memory>
#include <typeindex>
#include <chrono>
#include <type_traits>

namespace DaiSpan::Core {

/**
 * 基礎事件類別
 * 所有領域事件都必須繼承此類
 */
class DomainEvent {
public:
    virtual ~DomainEvent() = default;
    
    // 純虛函數：派生類必須提供其類型名稱
    virtual const char* getEventTypeName() const = 0;
    
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
    uint64_t eventId = generateEventId();
    
private:
    static uint64_t generateEventId() {
        static uint64_t counter = 0;
        return ++counter;
    }
};

/**
 * 事件發布器 - 專案的神經中樞
 * 負責所有領域間的通訊協調
 */
class EventPublisher {
public:
    EventPublisher() = default;
    ~EventPublisher() = default;
    
    // 禁止複製，確保單例性
    EventPublisher(const EventPublisher&) = delete;
    EventPublisher& operator=(const EventPublisher&) = delete;
    
    /**
     * 發布事件到事件佇列
     * @param event 要發布的事件
     */
    template<typename EventT>
    void publish(const EventT& event) {
        static_assert(std::is_base_of_v<DomainEvent, EventT>, "Event must inherit from DomainEvent");
        
        auto eventPtr = std::make_unique<EventT>(event);
        eventQueue.push(std::move(eventPtr));
        
        // DEBUG_VERBOSE_PRINT("[EventBus] 事件已發布: %s (ID: %llu)\n", 
        //                    typeid(EventT).name(), event.eventId);
    }
    
    /**
     * 訂閱特定類型的事件
     * @param handler 事件處理函數
     * @return 訂閱ID，可用於取消訂閱
     */
    template<typename EventT>
    uint32_t subscribe(std::function<void(const EventT&)> handler) {
        static_assert(std::is_base_of_v<DomainEvent, EventT>, "Event must inherit from DomainEvent");
        
        // 使用編譯時字串作為類型識別符，避免RTTI
        std::string typeId = EventT::EVENT_TYPE_NAME;
        auto wrappedHandler = [handler](const DomainEvent& event) {
            handler(static_cast<const EventT&>(event));
        };
        
        uint32_t subscriptionId = nextSubscriptionId++;
        handlers[typeId].emplace_back(subscriptionId, wrappedHandler);
        
        // DEBUG_INFO_PRINT("[EventBus] 新訂閱: %s (ID: %u)\n", 
        //                  typeId, subscriptionId);
        
        return subscriptionId;
    }
    
    /**
     * 取消事件訂閱
     * @param subscriptionId 訂閱ID
     */
    void unsubscribe(uint32_t subscriptionId) {
        for (auto& [typeId, handlerList] : handlers) {
            auto it = std::remove_if(handlerList.begin(), handlerList.end(),
                [subscriptionId](const auto& pair) {
                    return pair.first == subscriptionId;
                });
            
            if (it != handlerList.end()) {
                handlerList.erase(it, handlerList.end());
                // DEBUG_INFO_PRINT("[EventBus] 取消訂閱: ID %u\n", subscriptionId);
                return;
            }
        }
    }
    
    /**
     * 處理事件佇列 - 必須在主循環中定期調用
     * 限制每次處理的事件數量，避免阻塞
     */
    void processEvents(uint32_t maxEvents = 10) {
        uint32_t processedCount = 0;
        
        while (!eventQueue.empty() && processedCount < maxEvents) {
            auto event = std::move(eventQueue.front());
            eventQueue.pop();
            
            dispatchEvent(*event);
            processedCount++;
        }
        
        if (processedCount > 0) {
            // DEBUG_VERBOSE_PRINT("[EventBus] 處理了 %u 個事件\n", processedCount);
        }
    }
    
    /**
     * 獲取事件佇列統計
     */
    struct EventStatistics {
        size_t queueSize;
        uint32_t totalSubscriptions;
        uint64_t totalEventsProcessed;
    };
    
    EventStatistics getStatistics() const {
        uint32_t totalSubs = 0;
        for (const auto& [typeId, handlerList] : handlers) {
            totalSubs += handlerList.size();
        }
        
        return {
            .queueSize = eventQueue.size(),
            .totalSubscriptions = totalSubs,
            .totalEventsProcessed = eventsProcessed
        };
    }
    
    /**
     * 清空事件佇列 - 用於測試或緊急情況
     */
    void clearQueue() {
        std::queue<std::unique_ptr<DomainEvent>> empty;
        eventQueue.swap(empty);
        // DEBUG_WARN_PRINT("[EventBus] 事件佇列已清空\n");
    }
    
    // 向後兼容方法
    size_t getQueueSize() const { return eventQueue.size(); }
    uint32_t getSubscriptionCount() const { return getStatistics().totalSubscriptions; }
    uint64_t getProcessedEventCount() const { return eventsProcessed; }
    void resetStatistics() { eventsProcessed = 0; }

private:
    using HandlerFunction = std::function<void(const DomainEvent&)>;
    using HandlerEntry = std::pair<uint32_t, HandlerFunction>; // ID, Handler
    
    std::queue<std::unique_ptr<DomainEvent>> eventQueue;
    std::unordered_map<std::string, std::vector<HandlerEntry>> handlers;
    uint32_t nextSubscriptionId = 1;
    uint64_t eventsProcessed = 0;
    
    /**
     * 分發事件給所有訂閱者
     */
    void dispatchEvent(const DomainEvent& event) {
        // 需要派生類提供其類型名稱
        std::string typeId = event.getEventTypeName();
        auto it = handlers.find(typeId);
        
        if (it != handlers.end()) {
            for (const auto& [subscriptionId, handler] : it->second) {
                try {
                    handler(event);
                } catch (const std::exception& e) {
                    // DEBUG_ERROR_PRINT("[EventBus] 事件處理器異常 (ID: %u): %s\n", 
                    //                  subscriptionId, e.what());
                }
            }
            eventsProcessed++;
        }
    }
};

/**
 * 事件處理器基礎類別
 * 提供自動訂閱管理和生命週期處理
 */
class EventHandler {
public:
    explicit EventHandler(EventPublisher& eventBus) : eventBus_(eventBus) {}
    
    virtual ~EventHandler() {
        // 自動取消所有訂閱
        for (uint32_t subscriptionId : subscriptions_) {
            eventBus_.unsubscribe(subscriptionId);
        }
    }

protected:
    /**
     * 便利訂閱方法，自動管理訂閱ID
     */
    template<typename EventT>
    void subscribe(std::function<void(const EventT&)> handler) {
        uint32_t subscriptionId = eventBus_.subscribe<EventT>(handler);
        subscriptions_.push_back(subscriptionId);
    }
    
    /**
     * 發布事件的便利方法
     */
    template<typename EventT>
    void publish(const EventT& event) {
        eventBus_.publish(event);
    }

private:
    EventPublisher& eventBus_;
    std::vector<uint32_t> subscriptions_;
};

} // namespace DaiSpan::Core