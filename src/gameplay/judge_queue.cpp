// ──────────────────────────────────────────────────────
//  judge_queue.cpp — 多列判定队列实现
//  Tap/Hold 判定、Hold 尾部释放、过期自动 Miss、列分发与排序。
// ──────────────────────────────────────────────────────

#include "gameplay/judge_queue.h"  // 判定队列类与事件结构体声明
#include "util/logger.h"           // MM_LOG_* 日志宏

#include <cmath>       // std::abs：计算按键与音符的时间差绝对值
#include <algorithm>   // std::sort：各列音符按时间升序排列

namespace melody_matrix::gameplay {

// 构造函数：创建默认的标准判定策略实例
JudgeQueue::JudgeQueue()
    : m_strategy(std::make_unique<StandardJudgeStrategy>()) {}

/// 按 col 字段将音符分发到各列，并按时间升序排序
void JudgeQueue::loadNotes(const std::vector<beatmap::Note>& notes) {
    // 清空所有列的队列与 head 指针
    for (auto& col : m_columns) {
        col.reset();  // head=0 且 notes 向量清空
    }
    m_activeHolds = {};   // 重置所有列的 Hold 按住状态
    m_columnCount = 0;    // 有效列数归零，后续按音符 col 重新计算

    // 遍历谱面全部音符，按 col 字段分发到对应列子队列
    for (const auto& note : notes) {
        // col 越界则跳过并打警告，防止数组越界访问
        if (note.col < 0 || note.col >= MAX_COLS) {
            MM_LOG_WARN("JudgeQueue", "Note with invalid col=" + std::to_string(note.col) + " skipped");
            continue;  // 跳过非法列号的音符
        }
        m_columns[note.col].notes.push_back(note);  // 追加到目标列的待判定队列
        // 更新有效列数：取最大 col+1，保证 [0, m_columnCount) 覆盖所有有音符的列
        if (note.col + 1 > m_columnCount) {
            m_columnCount = note.col + 1;
        }
    }

    // 各列独立按音符时间升序排序，保证 front() 始终是下一颗待判定音符
    for (int32_t c = 0; c < m_columnCount; ++c) {
        auto& col = m_columns[c];  // 当前列的队列引用
        std::sort(col.notes.begin(), col.notes.end(),
                  [](const beatmap::Note& a, const beatmap::Note& b) { return a.time < b.time; });
    }

    // 统计加载的音符总数，用于日志输出
    size_t total = 0;
    for (int32_t c = 0; c < m_columnCount; ++c) {
        total += m_columns[c].notes.size();  // 累加每列音符数量
    }
    MM_LOG_INFO("JudgeQueue", "Loaded " + std::to_string(total) +
                " notes across " + std::to_string(m_columnCount) + " columns");
}

/// 替换判定策略；传入 nullptr 则回退到 StandardJudgeStrategy
void JudgeQueue::setStrategy(std::unique_ptr<IJudgeStrategy> strategy) {
    // 若传入有效策略则接管所有权，否则重建默认标准策略
    m_strategy = strategy ? std::move(strategy) : std::make_unique<StandardJudgeStrategy>();
}

/// Hold 尾部时序判定：与 Tap 共用 Perfect/Good 窗口
HoldReleaseResult JudgeQueue::judgeHoldTailTiming(int64_t dt, float od) const {
    const int32_t pw = m_strategy->perfectWindow(od);  // Perfect 窗口半宽（毫秒）
    const int32_t gw = m_strategy->goodWindow(od);     // Good 窗口半宽（毫秒）
    // dt 为释放时刻与 holdEnd 的差值，绝对值在 pw 内为 Perfect
    if (std::abs(dt) <= pw) return HoldReleaseResult::Perfect;
    // 超出 Perfect 但在 Good 窗口内
    if (std::abs(dt) <= gw) return HoldReleaseResult::Good;
    return HoldReleaseResult::Miss;  // 超出 Good 窗口视为 Miss
}

/// 提交 Hold 尾部判定：计算结果、推进队列、触发 onHoldTail
HoldReleaseResult JudgeQueue::commitHoldTail(int32_t column, int64_t releaseTimeMs, float od) {
    auto& hold = m_activeHolds[column];  // 该列当前 Hold 按住状态
    if (!hold.holding) {
        return HoldReleaseResult::Ignored;  // 该列无活跃 Hold，忽略释放
    }

    const int64_t dt = releaseTimeMs - hold.holdEndTimeMs;  // 松手时刻相对 Hold 结束点的偏差
    const HoldReleaseResult result = judgeHoldTailTiming(dt, od);  // 按窗口判定尾部精度

    // 组装 Hold 尾部事件，供上层（计分/特效）消费
    HoldTailEvent evt;
    evt.col = hold.col;                    // Hold 所在列
    evt.row = hold.row;                    // Hold 所在行
    evt.result = result;                   // 尾部判定结果
    evt.holdEndMs = hold.holdEndTimeMs;    // 谱面规定的 Hold 结束时间
    evt.releaseMs = releaseTimeMs;         // 玩家实际松手时间

    hold.holding = false;  // 清除按住标志，该列 Hold 判定流程结束
    auto& col = m_columns[column];  // 该列音符队列
    // 队头仍是 Hold 类型且队列未空时，推进 head 消费该 Hold 音符
    if (!col.finished() && col.front().type == beatmap::NoteType::Hold) {
        col.advance();  // head++，Hold 完整判定周期结束
    }

    if (onHoldTail) {
        onHoldTail(evt);  // 通知上层 Hold 尾部判定结果
    }
    return result;
}

/// 每帧更新：Hold 尾部超时自动 Miss；Tap 过期自动 Miss
void JudgeQueue::update(int64_t nowMs, float od) {
    const int64_t miss = m_strategy->missThreshold(od);  // Miss 阈值：音符时间 + miss 后过期

    // ── 第一阶段：Hold 尾部超时兜底 ──
    // 玩家按住 Hold 但超过 holdEnd + miss 仍未松手，强制按尾部 Miss 处理
    for (int32_t c = 0; c < m_columnCount; ++c) {
        auto& hold = m_activeHolds[c];  // 第 c 列的 Hold 状态
        // holding 为 true 且当前时间已超过 Hold 结束点 + Miss 缓冲
        if (hold.holding && nowMs > hold.holdEndTimeMs + miss) {
            commitHoldTail(c, nowMs, od);  // 以当前时刻作为释放时间提交尾部判定（通常为 Miss）
        }
    }

    // ── 第二阶段：Tap / 未按住的 Hold 过期自动 Miss ──
    for (int32_t c = 0; c < m_columnCount; ++c) {
        auto& col = m_columns[c];  // 第 c 列音符队列
        // 循环处理队头已过期的音符（可能连续多颗过期）
        while (!col.finished() && nowMs >= col.front().time + miss) {
            // 队头是 Hold 且玩家正在按住：不立即 Miss，等待松手或上方超时兜底
            if (col.front().type == beatmap::NoteType::Hold && m_activeHolds[c].holding) {
                break;  // 跳出 while，保留 Hold 在队头
            }
            emitMiss(c);  // 发出 Miss 事件并推进 head
        }
    }
}

/// 列上按键按下：Tap 即时判定；Hold 进入 active 状态，尾部另行判定
JudgmentResult JudgeQueue::onKeyPress(int64_t pressTimeMs, int32_t column, float od) {
    // ── 边界检查：列索引是否有效 ──
    if (column < 0 || column >= m_columnCount) {
        MM_LOG_WARN("JudgeQueue", "onKeyPress: column " + std::to_string(column) +
                    " out of range [0," + std::to_string(m_columnCount) + ")");
        return JudgmentResult::Ignored;  // 无效列，不消耗音符
    }

    auto& col = m_columns[column];  // 目标列的判定队列
    if (col.finished()) {
        // 该列所有音符已判定完毕，空按无效
        MM_LOG_DEBUG("JudgeQueue", "onKeyPress: col=" + std::to_string(column) +
                     " finished (head=" + std::to_string(col.head) +
                     " size=" + std::to_string(col.notes.size()) + ")");
        return JudgmentResult::Ignored;
    }

    const auto& note = col.front();  // 队头：当前待判定的下一颗音符
    const int32_t pw = m_strategy->perfectWindow(od);  // Perfect 半窗口（毫秒）
    const int32_t gw = m_strategy->goodWindow(od);     // Good 半窗口（毫秒）
    const int64_t dt = pressTimeMs - note.time;        // 正值=晚按，负值=早按

    // ── 过早判定：按键时刻早于 Good 窗口左边界 ──
    if (dt < -static_cast<int64_t>(gw)) {
        MM_LOG_DEBUG("JudgeQueue", "onKeyPress: col=" + std::to_string(column) +
                     " too early, dt=" + std::to_string(dt) + "ms" +
                     " gw=" + std::to_string(gw) + "ms" +
                     " noteTime=" + std::to_string(note.time) +
                     " pressTime=" + std::to_string(pressTimeMs));
        return JudgmentResult::Ignored;  // 太早，不消耗音符，等待后续按键或自动 Miss
    }

    // ── Tap 音符：按下即完成整颗音符判定 ──
    if (note.type == beatmap::NoteType::Tap) {
        if (std::abs(dt) <= pw) {
            commitHit(column, JudgmentResult::Perfect, pressTimeMs);  // 提交 Perfect 并推进 head
            return JudgmentResult::Perfect;
        }
        if (std::abs(dt) <= gw) {
            commitHit(column, JudgmentResult::Good, pressTimeMs);  // 提交 Good 并推进 head
            return JudgmentResult::Good;
        }
        // dt 在 (gw, miss) 之间：既不 Perfect/Good 也未过期，忽略本次按键
        MM_LOG_DEBUG("JudgeQueue", "onKeyPress: col=" + std::to_string(column) +
                     " between good and miss, dt=" + std::to_string(dt) + "ms" +
                     " gw=" + std::to_string(gw) + "ms");
        return JudgmentResult::Ignored;  // 等 update() 在过期时自动 Miss
    }

    // ── Hold 音符：按下只记录按住状态，head 不推进，尾部在释放/超时时判定 ──
    if (note.type == beatmap::NoteType::Hold) {
        if (m_activeHolds[column].holding) {
            return JudgmentResult::Ignored;  // 该列已有 Hold 在按住中，重复按键忽略
        }
        if (std::abs(dt) <= pw) {
            // Perfect 窗口内按下：进入 Hold 活跃状态
            auto& hold = m_activeHolds[column];
            hold.holding = true;              // 标记该列正在 Hold
            hold.pressTimeMs = pressTimeMs;   // 记录实际按下时间
            hold.holdEndTimeMs = note.holdEnd; // 谱面 Hold 结束时间点
            hold.row = note.row;              // 缓存行号供事件回调
            hold.col = note.col;              // 缓存列号供事件回调
            if (onHit) {
                NoteHitEvent evt;
                evt.result    = JudgmentResult::Perfect;  // Hold 头部判定为 Perfect
                evt.time      = note.time;    // 音符起始时间
                evt.row       = note.row;
                evt.col       = note.col;
                evt.pressTime = pressTimeMs;  // 实际按键时间
                onHit(evt);  // 触发击中回调（计分/连击/HP）
            }
            return JudgmentResult::Perfect;  // 返回头部判定结果，head 暂不推进
        }
        if (std::abs(dt) <= gw) {
            // Good 窗口内按下：同样进入 Hold 状态，头部判定为 Good
            auto& hold = m_activeHolds[column];
            hold.holding = true;
            hold.pressTimeMs = pressTimeMs;
            hold.holdEndTimeMs = note.holdEnd;
            hold.row = note.row;
            hold.col = note.col;
            if (onHit) {
                NoteHitEvent evt;
                evt.result    = JudgmentResult::Good;
                evt.time      = note.time;
                evt.row       = note.row;
                evt.col       = note.col;
                evt.pressTime = pressTimeMs;
                onHit(evt);
            }
            return JudgmentResult::Good;
        }
        return JudgmentResult::Ignored;  // 在 Good 与 Miss 之间，等待过期或重按
    }

    return JudgmentResult::Ignored;  // 未知音符类型，安全忽略
}

/// 列上按键释放：提交 Hold 尾部判定
HoldReleaseResult JudgeQueue::onKeyRelease(int64_t releaseTimeMs, int32_t column, float od) {
    if (column < 0 || column >= m_columnCount) {
        return HoldReleaseResult::Ignored;  // 列索引无效
    }
    // 直接委托 commitHoldTail：计算尾部时序、推进 head、触发 onHoldTail
    return commitHoldTail(column, releaseTimeMs, od);
}

/// 返回该列正在按住的 Hold 音符（head 在释放前不推进）
const beatmap::Note* JudgeQueue::getActiveHold(int32_t column) const {
    if (column < 0 || column >= m_columnCount) {
        return nullptr;  // 列越界
    }
    if (!m_activeHolds[column].holding) {
        return nullptr;  // 该列无活跃 Hold
    }
    // Hold 按下后 head 保持在该 Hold，释放/超时后才推进。
    auto& col = m_columns[column];
    if (col.finished() || col.notes.empty()) {
        return nullptr;  // 队列异常为空
    }
    return &col.front();  // 返回队头 Hold 音符指针供渲染层查询
}

// 是否所有列的音符都已判定完毕
bool JudgeQueue::finished() const {
    for (int32_t c = 0; c < m_columnCount; ++c) {
        if (!m_columns[c].finished()) {
            return false;  // 仍有列存在未判定音符
        }
    }
    return true;  // 全部列 head 已达 notes.size()
}

// 重置队列为空状态，用于新局或重开
void JudgeQueue::reset() {
    for (auto& col : m_columns) {
        col.reset();  // 清空每列队列
    }
    m_activeHolds = {};  // 清除所有 Hold 状态
    m_columnCount = 0;   // 有效列数归零
}

// 只读访问指定列的判定队列（供渲染器遍历可见音符）
const ColumnQueue& JudgeQueue::columnQueue(int32_t col) const {
    return m_columns[col];
}

// 统计所有列音符总数
size_t JudgeQueue::totalNotes() const {
    size_t total = 0;
    for (int32_t c = 0; c < m_columnCount; ++c) {
        total += m_columns[c].notes.size();
    }
    return total;
}

/// 提交 Tap 击中：触发 onHit 并推进列头
void JudgeQueue::commitHit(int32_t column, JudgmentResult result, int64_t pressTimeMs) {
    const auto& note = m_columns[column].front();  // 当前被击中的队头音符

    if (onHit) {
        // 组装击中事件并通知上层
        NoteHitEvent evt;
        evt.result    = result;       // Perfect 或 Good
        evt.time      = note.time;      // 谱面音符时间
        evt.row       = note.row;         // 行索引
        evt.col       = note.col;         // 列索引
        evt.pressTime = pressTimeMs;    // 玩家实际按键时间
        onHit(evt);  // 回调：计分、连击、HP、音效等
    }

    m_columns[column].advance();  // head++，该 Tap 音符判定完成
}

/// 发出 Miss 事件并推进列头
void JudgeQueue::emitMiss(int32_t column) {
    const auto& note = m_columns[column].front();  // 过期未击中的队头音符

    MM_LOG_DEBUG("JudgeQueue", "emitMiss: col=" + std::to_string(note.col) +
                 " row=" + std::to_string(note.row) +
                 " noteTime=" + std::to_string(note.time) + "ms");

    if (onMiss) {
        NoteMissEvent evt;
        evt.time = note.time;  // 音符时间
        evt.row  = note.row;   // 行索引
        evt.col  = note.col;   // 列索引
        onMiss(evt);  // 回调：连击归零、HP 扣减、Miss 特效等
    }

    m_columns[column].advance();  // head++，该音符以 Miss 结束
}

} // namespace melody_matrix::gameplay
