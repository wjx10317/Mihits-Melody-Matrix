// ──────────────────────────────────────────────────────
//  formation_controller.cpp — 阵型时间线实现
// ──────────────────────────────────────────────────────

#include "gameplay/formation_controller.h"  // FormationController 类与事件结构体
#include "util/logger.h"                    // MM_LOG_INFO / MM_LOG_DEBUG

#include <cmath>       // std::pow：ease-in-out 三次缓动后半段
#include <algorithm>   // std::sort：阵型按时间升序排列

namespace melody_matrix::gameplay {

/// 加载阵型并按时间排序
void FormationController::load(const std::vector<beatmap::Formation>& formations) {
    m_formations = formations;  // 拷贝谱面中的阵型时间线
    m_currentIndex = 0;         // 从第一个阵型开始

    // 确保按时间升序，便于 update() 线性扫描当前生效阵型
    std::sort(m_formations.begin(), m_formations.end(),
              [](const beatmap::Formation& a, const beatmap::Formation& b) {
                  return a.time < b.time;  // 时间戳小的排在前面
              });

    if (!m_formations.empty()) {
        // 日志输出阵型数量及初始网格尺寸（rows × cols）
        MM_LOG_INFO("FormationController", "Loaded " +
                    std::to_string(m_formations.size()) + " formations (" +
                    std::to_string(m_formations[0].rows) + "x" +
                    std::to_string(m_formations[0].cols) + " initial)");
    }
}

// 返回当前活动阵型的列数；无阵型时默认 4 列
int32_t FormationController::currentCols() const {
    if (m_formations.empty()) return 4;  // 空谱面回退默认值
    return m_formations[m_currentIndex].cols;  // 当前索引对应阵型的列数
}

/// 根据当前时间推进活动阵型索引；变化时触发 onFormationChanged
bool FormationController::update(int64_t nowMs) {
    if (m_formations.empty()) return false;  // 无阵型，无需更新

    size_t newIndex = m_currentIndex;  // 候选的新阵型索引，初始等于当前

    // 线性扫描：找到 time <= nowMs 的最大索引即为当前应生效的阵型
    for (size_t i = 0; i < m_formations.size(); ++i) {
        if (m_formations[i].time <= nowMs) {
            newIndex = i;  // 该阵型已生效，继续看是否有更晚的
        } else {
            break;  // 后续阵型时间更晚，停止扫描
        }
    }

    if (newIndex != m_currentIndex) {
        // ── 阵型发生切换 ──
        if (onFormationChanged) {
            FormationChangedEvent evt;
            evt.previous = m_formations[m_currentIndex];  // 切换前的旧阵型
            evt.current = m_formations[newIndex];         // 切换后的新阵型
            evt.transitionStartMs = nowMs;                // 转换动画起始时刻
            // 转换结束时刻 = 当前时刻 + 新阵型自带的 transformDurationMs
            evt.transitionEndMs = nowMs + m_formations[newIndex].transformDurationMs;
            onFormationChanged(evt);  // 通知渲染层开始网格变形动画
        }

        m_currentIndex = newIndex;       // 更新当前阵型索引
        m_transitionStartMs = nowMs;     // 记录转换开始时间，供 transitionProgress 使用
        MM_LOG_DEBUG("FormationController", "Formation changed to " +
                     std::to_string(m_formations[newIndex].rows) + "x" +
                     std::to_string(m_formations[newIndex].cols) + " at t=" +
                     std::to_string(nowMs) + "ms");
        return true;  // 告知调用方本帧发生了阵型变化
    }

    return false;  // 阵型未变化
}

/// 转换进度 0~1（ease-in-out cubic 插值前的线性 t）
float FormationController::transitionProgress(int64_t nowMs) const {
    if (m_transitionStartMs == 0) return 1.0f;  // 尚未开始转换，视为已完成

    // 优先使用当前阵型的 transformDurationMs，空谱面时用默认 300ms
    int64_t durationMs = m_formations.empty() ? m_transitionDurationMs
                                                : m_formations[m_currentIndex].transformDurationMs;
    int64_t elapsed = nowMs - m_transitionStartMs;  // 转换已过去的毫秒数
    if (elapsed >= durationMs) return 1.0f;  // 转换结束
    if (elapsed <= 0) return 0.0f;           // 尚未开始或时间倒退

    float t = static_cast<float>(elapsed) / static_cast<float>(durationMs);  // 线性进度 [0,1]
    return easeInOutCubic(t);  // 应用 S 曲线缓动后返回
}

// 当前是否处于阵型转换动画期间
bool FormationController::inTransition(int64_t nowMs) const {
    if (m_transitionStartMs == 0) return false;  // 无转换起点
    int64_t durationMs = m_formations.empty() ? m_transitionDurationMs
                                                : m_formations[m_currentIndex].transformDurationMs;
    return (nowMs - m_transitionStartMs) < durationMs;  // 未超过持续时间则在转换中
}

// 重置到初始状态（不重新加载谱面）
void FormationController::reset() {
    m_currentIndex = 0;        // 回到第一个阵型
    m_transitionStartMs = 0;   // 清除转换计时
}

/// ease-in-out 三次缓动：t∈[0,1] 映射到平滑 S 曲线
float FormationController::easeInOutCubic(float t) {
    return t < 0.5f
        ? 4.0f * t * t * t                              // 前半段：加速上升
        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;  // 后半段：减速趋近 1
}

} // namespace gameplay
