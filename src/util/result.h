/**
 * @file result.h
 * @brief 无异常的错误传递类型 Result<T>
 *
 * 文件职责：
 *   定义 Error 结构体、Result<T> 模板及 void 特化，替代可恢复错误场景的异常。
 *
 * 主要依赖：
 *   标准库 <variant>、<string>、<functional>。
 *
 * 在项目中的用法：
 *   函数返回 util::Result<std::string> 或 util::Result<void>；
 *   成功：return content 或 return util::success()；
 *   失败：return util::failure<T>(code, "原因")；
 *   调用方检查 if (result.ok()) 再访问 value()。
 */
#pragma once  // 防止头文件重复包含

#include <string>       // 错误消息字符串
#include <variant>      // 成功值与 Error 的联合存储
#include <functional>   // std::declval（map 中推断返回类型）

namespace melody_matrix::util {  // 工具层命名空间

/**
 * @brief Result<T> 在失败时携带的错误信息
 */
struct Error {
    int32_t code = 0;       ///< 错误码（通常对应 ErrorCode）
    std::string message;    ///< 人类可读描述

    Error() = default;
    Error(int32_t c, std::string msg) : code(c), message(std::move(msg)) {}  // 用错误码与消息构造

    /** @brief 有错误时为 true（code != 0） */
    explicit operator bool() const { return code != 0; }  // 非零错误码视为有错误
};

/**
 * @brief 成功/失败区分联合：成功持有 T，失败持有 Error
 *
 * 永不抛出异常，适用于 I/O、配置、路径校验等可恢复错误。
 * @tparam T 成功时的值类型
 */
template <typename T>
class Result {
public:
    // ── 成功构造函数 ──
    Result(T value) : m_data(std::move(value)) {}          // NOLINT  // 用成功值构造
    Result& operator=(T value) { m_data = std::move(value); return *this; }  // 赋值为成功值

    // ── 错误构造函数 ──
    Result(Error err) : m_data(std::move(err)) {}           // NOLINT  // 用 Error 构造失败 Result
    Result(int32_t code, std::string msg) : m_data(Error{code, std::move(msg)}) {}  // 用错误码与消息构造

    /**
     * @brief 是否成功
     * @return true 表示持有 T
     */
    bool ok() const { return std::holds_alternative<T>(m_data); }  // 检查 variant 是否持有 T
    explicit operator bool() const { return ok(); }  // 隐式转换为 bool，同 ok()

    // ── 访问器（失败分支时未定义行为）──
    T& value() & { return std::get<T>(m_data); }  // 左值引用访问成功值
    const T& value() const& { return std::get<T>(m_data); }  // 常量左值引用访问成功值
    T&& value() && { return std::get<T>(std::move(m_data)); }  // 右值引用访问成功值

    /**
     * @brief 获取错误信息
     * @return 失败时的 Error 引用
     */
    const Error& error() const { return std::get<Error>(m_data); }  // 获取失败时的 Error

    /**
     * @brief 成功返回值，失败返回 fallback
     * @param fallback 失败时的替代值
     * @return T 或 fallback
     */
    T value_or(T fallback) const {
        return ok() ? value() : std::move(fallback);  // 成功返回值，失败返回 fallback
    }

    /**
     * @brief 成功时映射值到新类型
     * @tparam F 映射函数类型
     * @param fn 接受 T 并返回 U 的函数
     * @return Result<U>，失败时透传原 Error
     */
    template <typename F>
    auto map(F&& fn) -> Result<decltype(fn(std::declval<T>()))> {
        using U = decltype(fn(std::declval<T>()));  // 推断映射后的值类型 U
        if (ok()) {  // 当前为成功状态
            return Result<U>(fn(value()));  // 对成功值执行映射并包装为 Result<U>
        }
        return Result<U>(error());  // 失败时透传原 Error
    }

private:
    std::variant<T, Error> m_data;
};

/**
 * @brief Result<void> 特化 — 仅表示成功/失败，无返回值
 */
template <>
class Result<void> {
public:
    Result() : m_ok(true) {}  // 默认构造表示成功
    Result(Error err) : m_ok(false), m_err(std::move(err)) {}  // NOLINT  // 用 Error 构造失败 Result
    Result(int32_t code, std::string msg) : m_ok(false), m_err(Error{code, std::move(msg)}) {}  // 用错误码与消息构造

    /** @return 是否成功 */
    bool ok() const { return m_ok; }  // 返回成功标志
    explicit operator bool() const { return m_ok; }  // 隐式转换为 bool，同 ok()

    /** @return 失败时的 Error */
    const Error& error() const { return m_err; }  // 获取失败时的 Error

private:
    bool m_ok;
    Error m_err;
};

/**
 * @brief 创建成功的 Result<void>
 * @return 表示成功的 Result<void>
 */
inline Result<void> success() { return Result<void>{}; }  // 返回表示成功的 Result<void>

/**
 * @brief 创建失败的 Result
 * @tparam T 值类型，默认 void
 * @param code 错误码
 * @param msg 错误消息
 * @return 携带 Error 的 Result<T>
 */
template <typename T = void>
Result<T> failure(int32_t code, std::string msg) {
    return Result<T>(Error{code, std::move(msg)});  // 构造携带 Error 的失败 Result
}

} // namespace melody_matrix::util
