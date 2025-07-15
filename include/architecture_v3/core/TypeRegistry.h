#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <functional>

namespace DaiSpan::Core {

/**
 * 替代 RTTI 的類型註冊系統
 */
class TypeRegistry {
public:
    using TypeId = std::string;
    
    template<typename T>
    static TypeId getTypeId() {
        return typeid(T).name(); // 僅用於編譯時生成唯一標識符
    }
    
    template<typename T>
    static TypeId registerType(const std::string& name) {
        auto typeId = getTypeId<T>();
        typeNames_[typeId] = name;
        return typeId;
    }
    
    static std::string getTypeName(const TypeId& typeId) {
        auto it = typeNames_.find(typeId);
        return it != typeNames_.end() ? it->second : "Unknown";
    }

private:
    static std::unordered_map<TypeId, std::string> typeNames_;
};

// 靜態成員定義
std::unordered_map<TypeRegistry::TypeId, std::string> TypeRegistry::typeNames_;

/**
 * 不依賴 RTTI 的事件類型系統
 */
template<typename T>
struct EventTypeId {
    static inline std::string id = T::EVENT_TYPE_NAME;
};

} // namespace DaiSpan::Core