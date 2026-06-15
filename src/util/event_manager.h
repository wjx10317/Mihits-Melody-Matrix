#pragma once

#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <any>
#include <typeindex>

namespace melody_matrix::util {

/// 类型擦除的事件总线（观察者模式）。
/// 按事件类型订阅，同步分派给所有订阅者。
/// 订阅顺序 = 调用顺序（保证游戏事件排序）。
class EventManager {
public:
    /// 订阅类型 T 的事件回调。
    /// 返回订阅 ID，用于将来可能的取消订阅。
    template <typename EventT>
    uint64_t subscribe(std::function<void(const EventT&)> callback) {
        auto key = std::type_index(typeid(EventT));
        uint64_t id = m_nextId++;
        m_subscribers[key].push_back({id, [cb = std::move(callback)](const std::any& e) {
                                         cb(std::any_cast<const EventT&>(e));
                                     }});
        return id;
    }

    /// 发出事件 — 此类型的所有订阅者按注册顺序调用。
    template <typename EventT>
    void emit(const EventT& event) {
        auto key = std::type_index(typeid(EventT));
        auto it = m_subscribers.find(key);
        if (it != m_subscribers.end()) {
            for (auto& sub : it->second) {
                sub.fn(event);
            }
        }
    }

    /// 移除所有订阅
    void clear() {
        m_subscribers.clear();
    }

private:
    struct Subscription {
        uint64_t id;
        std::function<void(const std::any&)> fn;
    };

    std::unordered_map<std::type_index, std::vector<Subscription>> m_subscribers;
    uint64_t m_nextId = 0;
};

} // namespace melody_matrix::util
