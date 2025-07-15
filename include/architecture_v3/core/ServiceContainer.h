#pragma once

#include <memory>
#include <unordered_map>
#include <typeindex>
#include <functional>
#include <stdexcept>

namespace DaiSpan::Core {

class ServiceContainer {
public:
    /**
     * 註冊單例服務
     */
    template<typename InterfaceT, typename ImplT, typename... Args>
    void registerSingleton(Args&&... args) {
        static_assert(std::is_base_of_v<InterfaceT, ImplT>, 
                     "Implementation must inherit from interface");
        
        auto factory = [args...](ServiceContainer& container) -> std::shared_ptr<void> {
            return std::make_shared<ImplT>(args...);
        };
        
        services_[std::type_index(typeid(InterfaceT))] = factory;
        // DEBUG_INFO_PRINT("[Container] 註冊單例: %s -> %s\n", 
        //                  typeid(InterfaceT).name(), typeid(ImplT).name());
    }
    
    /**
     * 註冊工廠函數
     */
    template<typename InterfaceT>
    void registerFactory(std::function<std::shared_ptr<InterfaceT>(ServiceContainer&)> factory) {
        auto wrappedFactory = [factory](ServiceContainer& container) -> std::shared_ptr<void> {
            return factory(container);
        };
        
        services_[std::type_index(typeid(InterfaceT))] = wrappedFactory;
    }
    
    /**
     * 解析服務
     */
    template<typename T>
    std::shared_ptr<T> resolve() {
        auto typeIndex = std::type_index(typeid(T));
        auto it = services_.find(typeIndex);
        
        if (it == services_.end()) {
            throw std::runtime_error("Service not registered: " + std::string(typeid(T).name()));
        }
        
        // 檢查是否已經創建（單例）
        auto singletonIt = singletons_.find(typeIndex);
        if (singletonIt != singletons_.end()) {
            return std::static_pointer_cast<T>(singletonIt->second);
        }
        
        // 創建新實例
        auto instance = it->second(*this);
        singletons_[typeIndex] = instance;
        
        return std::static_pointer_cast<T>(instance);
    }
    
    /**
     * 檢查服務是否已註冊
     */
    template<typename T>
    bool isRegistered() const {
        return services_.find(std::type_index(typeid(T))) != services_.end();
    }

private:
    using Factory = std::function<std::shared_ptr<void>(ServiceContainer&)>;
    std::unordered_map<std::type_index, Factory> services_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> singletons_;
};

} // namespace DaiSpan::Core