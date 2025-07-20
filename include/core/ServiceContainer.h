#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include <functional>
#include <stdexcept>

namespace DaiSpan::Core {

class ServiceContainer {
public:
    /**
     * 註冊單例服務 - 使用字符串作為鍵
     */
    template<typename InterfaceT, typename ImplT, typename... Args>
    void registerSingleton(const std::string& serviceName, Args&&... args) {
        static_assert(std::is_base_of_v<InterfaceT, ImplT>, 
                     "Implementation must inherit from interface");
        
        auto factory = [args...](ServiceContainer& container) -> std::shared_ptr<void> {
            return std::make_shared<ImplT>(args...);
        };
        
        services_[serviceName] = factory;
        // DEBUG_INFO_PRINT("[Container] 註冊單例: %s\n", serviceName.c_str());
    }
    
    /**
     * 註冊工廠函數 - 使用字符串作為鍵
     */
    template<typename InterfaceT>
    void registerFactory(const std::string& serviceName, std::function<std::shared_ptr<InterfaceT>(ServiceContainer&)> factory) {
        auto wrappedFactory = [factory](ServiceContainer& container) -> std::shared_ptr<void> {
            return factory(container);
        };
        
        services_[serviceName] = wrappedFactory;
    }
    
    /**
     * 解析服務 - 使用字符串作為鍵
     */
    template<typename T>
    std::shared_ptr<T> resolve(const std::string& serviceName) {
        auto it = services_.find(serviceName);
        
        if (it == services_.end()) {
            throw std::runtime_error("Service not registered: " + serviceName);
        }
        
        // 檢查是否已經創建（單例）
        auto singletonIt = singletons_.find(serviceName);
        if (singletonIt != singletons_.end()) {
            return std::static_pointer_cast<T>(singletonIt->second);
        }
        
        // 創建新實例
        auto instance = it->second(*this);
        singletons_[serviceName] = instance;
        
        return std::static_pointer_cast<T>(instance);
    }
    
    /**
     * 檢查服務是否已註冊
     */
    bool isRegistered(const std::string& serviceName) const {
        return services_.find(serviceName) != services_.end();
    }

private:
    using Factory = std::function<std::shared_ptr<void>(ServiceContainer&)>;
    std::unordered_map<std::string, Factory> services_;
    std::unordered_map<std::string, std::shared_ptr<void>> singletons_;
};

} // namespace DaiSpan::Core