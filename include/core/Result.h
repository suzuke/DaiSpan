#pragma once

#include <string>
#include <optional>
#include <stdexcept>

namespace DaiSpan::Core {

/**
 * Result<T> - 函數式錯誤處理類別
 * 
 * 靈感來自 Rust 的 Result<T, E> 和 Haskell 的 Either
 * 避免使用異常，提供明確的錯誤處理機制
 */
template<typename T>
class Result {
public:
    /**
     * 創建成功結果
     */
    static Result<T> success(const T& value) {
        return Result(value);
    }
    
    static Result<T> success(T&& value) {
        return Result(std::move(value));
    }
    
    /**
     * 創建失敗結果
     */
    static Result<T> failure(const std::string& error) {
        return Result(error);
    }
    
    static Result<T> failure(std::string&& error) {
        return Result(std::move(error));
    }
    
    /**
     * 檢查結果狀態
     */
    bool isSuccess() const noexcept {
        return value_.has_value();
    }
    
    bool isFailure() const noexcept {
        return !isSuccess();
    }
    
    /**
     * 獲取成功值（如果失敗則拋出異常）
     */
    const T& getValue() const {
        if (!isSuccess()) {
            throw std::runtime_error("Attempted to get value from failed Result: " + error_);
        }
        return value_.value();
    }
    
    T& getValue() {
        if (!isSuccess()) {
            throw std::runtime_error("Attempted to get value from failed Result: " + error_);
        }
        return value_.value();
    }
    
    /**
     * 獲取錯誤訊息（如果成功則拋出異常）
     */
    const std::string& getError() const {
        if (isSuccess()) {
            throw std::runtime_error("Attempted to get error from successful Result");
        }
        return error_;
    }
    
    /**
     * 安全獲取值，如果失敗則返回預設值
     */
    T getValueOr(const T& defaultValue) const {
        return isSuccess() ? value_.value() : defaultValue;
    }
    
    /**
     * 安全獲取值，如果失敗則使用工廠函數創建預設值
     */
    template<typename F>
    T getValueOr(F&& factory) const {
        return isSuccess() ? value_.value() : factory();
    }
    
    /**
     * 鏈式操作：如果成功則應用函數，否則傳播錯誤
     */
    template<typename F>
    auto map(F&& func) -> Result<decltype(func(std::declval<T>()))> {
        using ReturnType = decltype(func(std::declval<T>()));
        
        if (isSuccess()) {
            return Result<ReturnType>::success(func(getValue()));
        } else {
            return Result<ReturnType>::failure(error_);
        }
    }
    
    /**
     * 鏈式操作：如果成功則應用返回 Result 的函數（flatMap）
     */
    template<typename F>
    auto flatMap(F&& func) -> decltype(func(std::declval<T>())) {
        using ReturnType = decltype(func(std::declval<T>()));
        
        if (isSuccess()) {
            return func(getValue());
        } else {
            return ReturnType::failure(error_);
        }
    }
    
    /**
     * 錯誤恢復：如果失敗則應用恢復函數
     */
    template<typename F>
    Result<T> recover(F&& recoveryFunc) const {
        if (isFailure()) {
            return recoveryFunc(error_);
        }
        return *this;
    }
    
    /**
     * 副作用操作：對成功值執行操作但不改變 Result
     */
    template<typename F>
    const Result<T>& onSuccess(F&& func) const {
        if (isSuccess()) {
            func(getValue());
        }
        return *this;
    }
    
    /**
     * 副作用操作：對錯誤執行操作但不改變 Result
     */
    template<typename F>
    const Result<T>& onFailure(F&& func) const {
        if (isFailure()) {
            func(error_);
        }
        return *this;
    }
    
    /**
     * 轉換為 optional
     */
    std::optional<T> toOptional() const {
        return isSuccess() ? std::make_optional(getValue()) : std::nullopt;
    }

private:
    std::optional<T> value_;
    std::string error_;
    
    // 私有建構函數
    explicit Result(const T& value) : value_(value) {}
    explicit Result(T&& value) : value_(std::move(value)) {}
    explicit Result(const std::string& error) : error_(error) {}
    explicit Result(std::string&& error) : error_(std::move(error)) {}
};

/**
 * Result<void> 的特化版本
 * 用於不返回值但可能失敗的操作
 */
template<>
class Result<void> {
public:
    static Result<void> success() {
        return Result();
    }
    
    static Result<void> failure(const std::string& error) {
        return Result(error);
    }
    
    static Result<void> failure(std::string&& error) {
        return Result(std::move(error));
    }
    
    bool isSuccess() const noexcept {
        return error_.empty();
    }
    
    bool isFailure() const noexcept {
        return !isSuccess();
    }
    
    const std::string& getError() const {
        if (isSuccess()) {
            throw std::runtime_error("Attempted to get error from successful Result");
        }
        return error_;
    }
    
    /**
     * 鏈式操作：如果成功則應用函數，否則傳播錯誤
     */
    template<typename F>
    auto map(F&& func) -> Result<decltype(func())> {
        using ReturnType = decltype(func());
        
        if (isSuccess()) {
            return Result<ReturnType>::success(func());
        } else {
            return Result<ReturnType>::failure(error_);
        }
    }
    
    /**
     * 鏈式操作：如果成功則應用返回 Result 的函數
     */
    template<typename F>
    auto flatMap(F&& func) -> decltype(func()) {
        if (isSuccess()) {
            return func();
        } else {
            return decltype(func())::failure(error_);
        }
    }
    
    /**
     * 副作用操作：如果成功則執行操作
     */
    template<typename F>
    const Result<void>& onSuccess(F&& func) const {
        if (isSuccess()) {
            func();
        }
        return *this;
    }
    
    /**
     * 副作用操作：如果失敗則執行操作
     */
    template<typename F>
    const Result<void>& onFailure(F&& func) const {
        if (isFailure()) {
            func(error_);
        }
        return *this;
    }

private:
    std::string error_;
    
    Result() = default; // 成功建構函數
    explicit Result(const std::string& error) : error_(error) {}
    explicit Result(std::string&& error) : error_(std::move(error)) {}
};

/**
 * 便利函數：組合多個 Result
 */
template<typename... Results>
Result<void> combine(Results&&... results) {
    auto checkResult = [](const auto& result) {
        if (result.isFailure()) {
            return Result<void>::failure(result.getError());
        }
        return Result<void>::success();
    };
    
    auto combined = Result<void>::success();
    ((combined = combined.flatMap([&]() { return checkResult(results); })), ...);
    
    return combined;
}

/**
 * 便利函數：將可能拋出異常的操作包裝為 Result
 */
template<typename F>
auto tryExecute(F&& func) -> Result<decltype(func())> {
    using ReturnType = decltype(func());
    
    try {
        if constexpr (std::is_void_v<ReturnType>) {
            func();
            return Result<void>::success();
        } else {
            return Result<ReturnType>::success(func());
        }
    } catch (const std::exception& e) {
        if constexpr (std::is_void_v<ReturnType>) {
            return Result<void>::failure(e.what());
        } else {
            return Result<ReturnType>::failure(e.what());
        }
    }
}

} // namespace DaiSpan::Core