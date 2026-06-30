#pragma once

// ──────────────────────────────────────────────────────
//  formation_controller.h — 阵型时间线控制
//  按谱面 Formation 序列切换网格尺寸，发出变换事件，
//  并提供转换进度与 ease-in-out 缓动供渲染插值。
// ──────────────────────────────────────────────────────

#include "beatmap/beatmap.h"  // beatmap::Formation 阵型结构体

#include <vector>      // 阵型时间线存储
#include <cstdint>     // int64_t 等固定宽度整数
#include <functional>  // onFormationChanged 回调

namespace melody_matrix::gameplay {

/// 阵型变化事件
struct FormationChangedEvent {
    beatmap::Formation previous;   ///< 切换前的阵型（行/列/时间等）
    beatmap::Formation current;    ///< 切换后的新阵型
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

    /// 获取当前阵型的列数（便捷方法）
    int32_t currentCols() const;

    /// 获取当前阵型索引
    size_t currentIndex() const { return m_currentIndex; }

    /// 获取阵型数量
    size_t formationCount() const { return m_formations.size(); }

    /// 获取指定索引的阵型
    const beatmap::Formation& formationAt(size_t idx) const { return m_formations[idx]; }

    /// 获取下一次阵型变换时间（无后续变换返回 INT64_MAX）
    int64_t nextFormationTime() const {
        if (m_currentIndex + 1 < m_formations.size()) {
            return m_formations[m_currentIndex + 1].time;  // 下一个阵型的时间戳
        }
        return INT64_MAX;  // 无后续变换，返回最大值表示"永不"
    }

    /// 计算转换进度（0.0 = 刚刚开始，1.0 = 完成）
    float transitionProgress(int64_t nowMs) const;

    /// 当前是否正在转换中
    bool inTransition(int64_t nowMs) const;

    /// 设置转换持续时间
    void setTransitionDuration(int64_t durationMs) { m_transitionDurationMs = durationMs; }

    /// 重置状态
    void reset();

    /// 阵型变化的事件回调
    std::function<void(const FormationChangedEvent&)> onFormationChanged;

private:
    std::vector<beatmap::Formation> m_formations;  ///< 按时间排序的阵型序列
    size_t m_currentIndex = 0;                     ///< 当前生效的阵型索引
    int64_t m_transitionStartMs = 0;               ///< 最近一次转换的起始时刻
    int64_t m_transitionDurationMs = 300; // 默认 300ms 转换（空谱面回退用）

    /// 缓动函数：ease-in-out cubic
    static float easeInOutCubic(float t);
};

} // namespace gameplay
