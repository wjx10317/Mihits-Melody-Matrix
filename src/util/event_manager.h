/**
 * @file event_manager.h
 * @brief 类型擦除的事件总线（观察者模式）
 *
 * 文件职责：
 *   提供按事件类型订阅/分派的轻量级 EventManager，解耦模块间通信。
 *
 * 主要依赖：
 *   标准库 <functional>、<unordered_map>、<any>、<typeindex>。
 *
 * 在项目中的用法：
 *   Kernel 持有 EventManager 实例；各模块通过 kernel.eventManager().subscribe<MyEvent>(...)
 *   订阅，通过 emit() 同步广播。订阅顺序即调用顺序，保证游戏事件时序。
 */
#pragma once

#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <any>
#include <typeindex>

namespace melody_matrix::util {

/**
 * @brief 类型擦除的事件总线
 *
 * 按事件类型（typeid）注册回调，emit 时同步分派给所有订阅者。
 * 使用 std::any 擦除具体事件类型，模板方法在编译期恢复类型。
 */
class EventManager {
public:
    /**
     * @brief 订阅指定类型的事件
     * @tparam EventT 事件结构体/类类型
     * @param callback 收到事件时的处理函数
     * @return 订阅 ID（预留用于将来取消订阅）
     */
    template <typename EventT>
    uint64_t subscribe(std::function<void(const EventT&)> callback) {
        auto key = std::type_index(typeid(EventT));
        uint64_t id = m_nextId++;
        m_subscribers[key].push_back({id, [cb = std::move(callback)](const std::any& e) {
                                         cb(std::any_cast<const EventT&>(e));
                                     }});
        return id;
    }

    /**
     * @brief 发出事件，按注册顺序调用所有订阅者
     * @tparam EventT 事件类型
     * @param event 事件实例（const 引用）
     */
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

    /** @brief 移除所有类型的全部订阅 */
    void clear() {
        m_subscribers.clear();
    }

private:
    /** @brief 单条订阅记录：ID + 类型擦除后的回调 */
    struct Subscription {
        uint64_t id;
        std::function<void(const std::any&)> fn;
    };

    std::unordered_map<std::type_index, std::vector<Subscription>> m_subscribers;
    uint64_t m_nextId = 0;
};

} // namespace melody_matrix::util
