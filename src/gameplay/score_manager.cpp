// ──────────────────────────────────────────────────────
//  score_manager.cpp — 分数计算实现
// ──────────────────────────────────────────────────────

#include "gameplay/score_manager.h"  // ScoreManager 类声明

#include <cmath>       // std::log2f：连击倍率对数曲线
#include <algorithm>   // （预留）数值 clamp 等

namespace melody_matrix::gameplay {

/// 单次击中的得分（静态，供预览与 addScore 共用）
int32_t ScoreManager::scoreForHit(JudgmentResult result, int32_t combo) {
    float acc = 0.0f;  // 判定精度系数
    switch (result) {
    case JudgmentResult::Perfect: acc = 1.0f;    break;  // 满分精度
    case JudgmentResult::Good:    acc = 0.6667f; break;  // 约 2/3 精度
    case JudgmentResult::Miss:    acc = 0.3333f; break;  // 约 1/3 精度（Miss 仍计分但低）
    default: return 0;  // Ignored 等无效判定不计分
    }

    // 连击倍率：combo 越高倍率越大，log2 曲线避免爆炸式增长
    const float mult = 1.0f + std::log2f(static_cast<float>(combo + 1)) * 0.2f;
    return static_cast<int32_t>(BASE_SCORE * acc * mult);  // 基础分 × 精度 × 连击倍率
}

/// 累加得分并更新命中统计（Miss 不计入 hitNotes）
void ScoreManager::addScore(JudgmentResult result, int32_t combo) {
    if (result == JudgmentResult::Ignored) return;  // 无效按键不影响分数

    m_totalScore += scoreForHit(result, combo);  // 累加本次得分

    // Miss 不计入 hitNotes（仅统计实际击中次数）
    if (result != JudgmentResult::Miss) {
        ++m_hitNotes;
    }
    ++m_totalNotes;  // 总判定音符数（含 Miss）
}

/// 加权准确度：totalScore / (totalNotes × BASE_SCORE)
float ScoreManager::accuracy() const {
    if (m_totalNotes == 0) return 1.0f;  // 尚无判定，默认 100%
    // 加权准确度公式：totalScore / (totalNotes * BASE_SCORE)
    return static_cast<float>(m_totalNotes * BASE_SCORE) > 0 ?
        static_cast<float>(m_totalScore) / static_cast<float>(m_totalNotes * BASE_SCORE) : 1.0f;
}

/// 预留接口：最大可能分在 addScore 流程中维护
void ScoreManager::recordNoteAvailable() {
    // 不再使用 — m_totalNotes 在 addScore() 中维护
}

// 新局重置所有计分统计
void ScoreManager::reset() {
    m_totalScore  = 0;  // 总分归零
    m_maxPossible = 0;  // 最大可能分（预留）
    m_totalNotes  = 0;  // 判定音符总数
    m_hitNotes    = 0;  // 击中音符数
}

} // namespace melody_matrix::gameplay
