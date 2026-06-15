#pragma once

#include <string>
#include <variant>
#include <functional>

namespace melody_matrix::util {

/// Result<T> 在失败时携带的错误信息
struct Error {
    int32_t code = 0;
    std::string message;

    Error() = default;
    Error(int32_t c, std::string msg) : code(c), message(std::move(msg)) {}

    explicit operator bool() const { return code != 0; }
};

/// 区分联合：成功时持有 T，失败时持有 Error。
/// 永不抛出异常 — 对于可恢复错误使用此类而非异常。
template <typename T>
class Result {
public:
    // ── 成功构造函数 ──
    Result(T value) : m_data(std::move(value)) {}          // NOLINT
    Result& operator=(T value) { m_data = std::move(value); return *this; }

    // ── 错误构造函数 ──
    Result(Error err) : m_data(std::move(err)) {}           // NOLINT
    Result(int32_t code, std::string msg) : m_data(Error{code, std::move(msg)}) {}

    // ── 查询 ──
    bool ok() const { return std::holds_alternative<T>(m_data); }
    explicit operator bool() const { return ok(); }

    // ── 访问器（错误分支时未定义行为）──
    T& value() & { return std::get<T>(m_data); }
    const T& value() const& { return std::get<T>(m_data); }
    T&& value() && { return std::get<T>(std::move(m_data)); }

    const Error& error() const { return std::get<Error>(m_data); }

    /// 返回值或默认构造的 T
    T value_or(T fallback) const {
        return ok() ? value() : std::move(fallback);
    }

    /// 如果成功则映射值
    template <typename F>
    auto map(F&& fn) -> Result<decltype(fn(std::declval<T>()))> {
        using U = decltype(fn(std::declval<T>()));
        if (ok()) {
            return Result<U>(fn(value()));
        }
        return Result<U>(error());
    }

private:
    std::variant<T, Error> m_data;
};

/// void 特化 — 仅成功/失败
template <>
class Result<void> {
public:
    Result() : m_ok(true) {}
    Result(Error err) : m_ok(false), m_err(std::move(err)) {}  // NOLINT
    Result(int32_t code, std::string msg) : m_ok(false), m_err(Error{code, std::move(msg)}) {}

    bool ok() const { return m_ok; }
    explicit operator bool() const { return m_ok; }
    const Error& error() const { return m_err; }

private:
    bool m_ok;
    Error m_err;
};

/// 便捷函数：创建成功的 Result<void>
inline Result<void> success() { return Result<void>{}; }

/// 便捷函数：创建错误 Result
template <typename T = void>
Result<T> failure(int32_t code, std::string msg) {
    return Result<T>(Error{code, std::move(msg)});
}

} // namespace melody_matrix::util
