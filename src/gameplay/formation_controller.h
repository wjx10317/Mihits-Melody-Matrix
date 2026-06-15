#pragma once

#include "beatmap/beatmap.h"

#include <vector>
#include <cstdint>
#include <functional>

namespace melody_matrix::gameplay {

/// 阵型变化事件
struct FormationChangedEvent {
    beatmap::Formation previous;
    beatmap::Formation current;
    int64_t transitionStartMs; ///< 转换开始时间
    int64_t transitionEndMs;   ///< 转换结束时间
};

/// 阵型控制器 — 管理阵型时间线和转换。
/// 持有阵型序列（按时间排序）并跟踪活动阵型。
/// 当时钟经过阵型边界时，发出 FormationChangedEvent。
class FormationController {
public:
    FormationController() = default;

    /// 从谱面加载阵型
    void load(const std::vector<beatmap::Formation>& formations);

    /// 根据时间更新当前阵型。如果阵型变化返回 true。
    bool update(int64_t nowMs);

    /// 获取当前活动阵型
    const beatmap::Formation& current() const { return m_formations[m_currentIndex]; }

    /// 获取当前阵型索引
    size_t currentIndex() const { return m_currentIndex; }

    /// 获取阵型数量
    size_t formationCount() const { return m_formations.size(); }

    /// 获取指定索引的阵型
    const beatmap::Formation& formationAt(size_t idx) const { return m_formations[idx]; }

    /// 计算转换进度（0.0 = 刚刚开始，1.0 = 完成）
    float transitionProgress(int64_t nowMs) const;

    /// 当前是否正在转换中
    bool inTransition(int64_t nowMs) const;

    /// 获取转换持续时间（毫秒）
    int64_t transitionDuration() const { return m_transitionDurationMs; }

    /// 设置转换持续时间
    void setTransitionDuration(int64_t durationMs) { m_transitionDurationMs = durationMs; }

    /// 将网格单元映射到屏幕坐标
    static void cellToScreen(int row, int col, int rows, int cols,
                             float& outX, float& outY);

    /// 重置状态
    void reset();

    /// 阵型变化的事件回调
    std::function<void(const FormationChangedEvent&)> onFormationChanged;

private:
    std::vector<beatmap::Formation> m_formations;
    size_t m_currentIndex = 0;
    int64_t m_transitionStartMs = 0;
    int64_t m_transitionDurationMs = 300; // 默认 300ms 转换

    /// 缓动函数：ease-in-out cubic
    static float easeInOutCubic(float t);
};

} // namespace gameplay
