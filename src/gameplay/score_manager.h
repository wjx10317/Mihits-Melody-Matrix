#pragma once

// ──────────────────────────────────────────────────────
//  score_manager.h — 分数与准确度
//  按判定精度与连击倍率累积分；维护加权准确度。
//  公式见设计文档 8.1：BASE_SCORE × accuracy × comboMultiplier
// ──────────────────────────────────────────────────────

#include "gameplay/judge_strategy.h"  // JudgmentResult 枚举
#include "gameplay/combo_manager.h"   // 连击倍率公式与 ComboManager 协同

#include <cstdint>  // int32_t / int64_t

namespace melody_matrix::gameplay {

/// 分数管理器 — 根据判定结果和连击累积分数。
/// 分数公式（来自设计文档 8.1）：
///   scoreForHit = BASE_SCORE * accuracy * comboMultiplier
///   其中 accuracy: Perfect=1.0, Good=0.6667, Miss=0.3333
///   comboMultiplier = 1.0 + log2(combo+1) * 0.2
class ScoreManager {
public:
    /// 计算给定连击等级下击中的分数
    static int32_t scoreForHit(JudgmentResult result, int32_t combo);

    /// 添加击中的分数
    void addScore(JudgmentResult result, int32_t combo);

    /// 获取总分
    int64_t totalScore() const { return m_totalScore; }

    /// 获取最大可能分数（用于准确度计算）
    int64_t maxPossibleScore() const { return m_maxPossible; }

    /// 获取当前准确度（0.0 - 1.0）
    float accuracy() const;

    /// 记录一个音符可用（用于跟踪最大可能分数）
    void recordNoteAvailable();

    /// 为新游玩重置
    void reset();

private:
    static constexpr int32_t BASE_SCORE = 300;  ///< 单音符 Perfect 基础分（combo=0 时）

    int64_t m_totalScore  = 0;   ///< 累计总分
    int64_t m_maxPossible = 0;   ///< 所有音符在其连击时完美得分的总和
    int32_t m_totalNotes  = 0;   ///< 已判定音符总数
    int32_t m_hitNotes    = 0;   ///< 非 Miss 击中数
};

} // namespace melody_matrix::gameplay
