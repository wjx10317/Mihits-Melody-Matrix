#include "beatmap/osu_parser.h"
#include "beatmap/beatmap_parser.h"
#include "beatmap/mma_parser.h"
#include "beatmap/scroll_simulation.h"
#include "util/logger.h"
#include "util/error_codes.h"

#include <sstream>
#include <cmath>
#include <algorithm>

// ──────────────────────────────────────────────────────
//  osu_parser.cpp — osu! 谱面转换实现
//
//  流水线：
//    1. parse TimingPoints/HitObjects
//    2. makeConvertedNotes（Tap/Hold + makeWindow，approach/hit50 与 scroll_simulation 共用）
//    3. generateFormationsAndFilter
//         - 纯滚动：resolveScrollConflict（前向阻塞 → 推迟/降级/丢前向；不丢后向）
//         - 变阵：scheduleTransitionBefore（maxBlockingLatestHitBefore）
//    4. arrangeRemainingNotes（稳定列/边缘列；moveAffectsScroll 用 blockingLatestHit）
//    5. 写入 BeatmapBuilder
//
//  阻塞语义（noteBlocksUntilMs）：Tap=time+hit50；Hold=holdEnd（对齐运行时自动 300）
// ──────────────────────────────────────────────────────

namespace melody_matrix::beatmap {

// ── 字符串与曲线解析工具 ──

/// 去除首尾空白
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, delim)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

/// 解析 Slider 曲线点字符串，如 "B|200:100|300:50" → curveType + 点列表
static void parseSliderCurve(const std::string& curveStr,
                              char& outCurveType,
                              std::vector<std::pair<int,int>>& outPoints) {
    if (curveStr.empty()) return;
    outCurveType = curveStr[0];
    auto parts = split(curveStr.substr(1), '|');
    for (const auto& p : parts) {
        if (p.empty()) continue;
        auto coords = split(p, ':');
        if (coords.size() >= 2) {
            try {
                outPoints.push_back({std::stoi(coords[0]), std::stoi(coords[1])});
            } catch (...) {}
        }
    }
}

// ── 像素坐标 → 网格坐标 ──

/// osu 像素 (x,y) 映射到 rows×cols 网格；边界用 511.999/383.999 clamp
void OsuParser::pixelToGrid(int x, int y, int32_t rows, int32_t cols,
                             int32_t& outRow, int32_t& outCol) {
    // 对齐参考转换器：用 511.999/383.999 clamp，避免边界像素溢出
    const double clampedX = std::max(0.0, std::min(511.999, static_cast<double>(x)));
    const double clampedY = std::max(0.0, std::min(383.999, static_cast<double>(y)));
    outCol = static_cast<int32_t>(std::floor(clampedX / 512.0 * cols));
    outRow = static_cast<int32_t>(std::floor(clampedY / 384.0 * rows));
    outCol = std::max(0, std::min(outCol, cols - 1));
    outRow = std::max(0, std::min(outRow, rows - 1));
}

// ── TimingPoint 解析与 BPM/SV 查询 ──

/// 解析 [TimingPoints] 段 CSV 行，区分独立型/继承型
void OsuParser::parseTimingPoints(const std::vector<std::string>& lines) {
    m_timingPoints.clear();
    for (const auto& line : lines) {
        auto parts = split(line, ',');
        if (parts.size() < 2) continue;

        TimingPoint tp;
        try {
            tp.offset = std::stoll(parts[0]);
            tp.msPerBeat = std::stod(parts[1]);
            if (parts.size() >= 3) tp.meter = std::stoi(parts[2]);
            if (parts.size() >= 7) tp.uninherited = (std::stoi(parts[6]) != 0);
        } catch (...) {
            continue;
        }

        m_timingPoints.push_back(tp);
    }

    // 确保按时间排序
    std::sort(m_timingPoints.begin(), m_timingPoints.end(),
              [](const TimingPoint& a, const TimingPoint& b) { return a.offset < b.offset; });

    MM_LOG_INFO("OsuParser", "Parsed " + std::to_string(m_timingPoints.size()) + " timing points");
}

const OsuParser::TimingPoint* OsuParser::getBaseTimingPoint(int64_t time) const {
    const TimingPoint* result = nullptr;
    for (const auto& tp : m_timingPoints) {
        if (tp.offset <= time && tp.uninherited) {
            result = &tp;
        } else if (tp.offset > time) {
            break;
        }
    }
    return result;
}

/// 指定时刻的 ms/beat：独立型基准 × 继承型 SV 倍率（msPerBeat<0 时）
double OsuParser::getMsPerBeatAt(int64_t time) const {
    // 找最近的独立型 TimingPoint 作为 BPM 基准
    const TimingPoint* base = getBaseTimingPoint(time);
    if (!base) return 60000.0 / 120.0; // 默认 120 BPM = 500ms/beat

    // 检查是否有继承型 TimingPoint 覆盖
    const TimingPoint* inherited = nullptr;
    for (const auto& tp : m_timingPoints) {
        if (tp.offset <= time && !tp.uninherited) {
            inherited = &tp;
        } else if (tp.offset > time) {
            break;
        }
    }

    if (inherited && inherited->msPerBeat < 0) {
        // 继承型：速度倍率 = -100 / msPerBeat
        double multiplier = -100.0 / inherited->msPerBeat;
        return base->msPerBeat * multiplier;
    }

    return base->msPerBeat;
}

/// BPM → 编排细分分母：≥140 → 1/3；100–140 → 1/4；<100 → 1/6。
double OsuParser::arrangeRhythmSubdivDenominator(double bpm) const {
    if (bpm >= kArrangeSubdivBpmHigh) {
        return 3.0;
    }
    if (bpm >= kArrangeSubdivBpmMid) {
        return 4.0;
    }
    return 6.0;
}

int64_t OsuParser::arrangeRhythmWindowMs(int64_t timeMs) const {
    const TimingPoint* base = getBaseTimingPoint(timeMs);
    const double msPerBeat = base ? base->msPerBeat : 60000.0 / 120.0;
    const double bpm = 60000.0 / msPerBeat;
    const double denom = arrangeRhythmSubdivDenominator(bpm);
    return static_cast<int64_t>(std::llround(msPerBeat / denom));
}

// ── HitObject 解析 ──

/// 解析 [HitObjects]：Circle/Slider/Spinner 字段与曲线点
void OsuParser::parseHitObjects(const std::vector<std::string>& lines) {
    m_rawObjects.clear();
    for (const auto& line : lines) {
        auto parts = split(line, ',');
        if (parts.size() < 4) continue;

        RawHitObject obj;
        try {
            obj.x = std::stoi(parts[0]);
            obj.y = std::stoi(parts[1]);
            obj.time = std::stoll(parts[2]);
            obj.type = std::stoi(parts[3]);
        } catch (...) {
            continue;
        }

        // Slider: 解析曲线、slides、length
        if (obj.type & 2) {
            if (parts.size() >= 8) {
                parseSliderCurve(parts[5], obj.curveType, obj.curvePoints);
                try {
                    obj.slides = std::stoi(parts[6]);
                    obj.length = std::stod(parts[7]);
                } catch (...) {
                    obj.slides = 1;
                    obj.length = 0.0;
                }
            }
        }

        // Spinner: 解析 endTime
        if (obj.type & 4) {
            if (parts.size() >= 6) {
                try { obj.endTime = std::stoll(parts[5]); } catch (...) {}
            }
        }

        m_rawObjects.push_back(obj);
    }

    // 按时间排序
    std::sort(m_rawObjects.begin(), m_rawObjects.end(),
              [](const RawHitObject& a, const RawHitObject& b) { return a.time < b.time; });

    MM_LOG_INFO("OsuParser", "Parsed " + std::to_string(m_rawObjects.size()) + " hit objects");
}

// ── 转换辅助（对齐 osz_to_mma.cpp）──

/// 指定时刻的 Slider 速度倍率（继承型 TP：-100/msPerBeat）
double OsuParser::getSliderVelocityAt(int64_t time) const {
    // 获取继承型 TimingPoint 的 SV 倍率（-100 / msPerBeat）
    double sliderVelocity = 1.0;
    for (const auto& tp : m_timingPoints) {
        if (tp.offset > time) break;
        if (!tp.uninherited && tp.msPerBeat < 0.0) {
            sliderVelocity = std::max(0.1, -100.0 / tp.msPerBeat);
        }
    }
    return sliderVelocity;
}

/// 构造 note 显示/判定窗口：displayStart、earliestHit、latestHit
OsuParser::NoteWindow OsuParser::makeWindow(int64_t time) const {
    // approach / 50 窗与 scroll_simulation 共用公式
    const int64_t approach = approachMs(m_ar);
    const int32_t hit50W = hit50WindowMs(m_od);
    NoteWindow w;
    w.displayStart = time - approach;
    w.earliestHit = time - static_cast<int64_t>(hit50W);
    w.latestHit = time + static_cast<int64_t>(hit50W);
    return w;
}

/// Slider 结束时间：length×slides / (SliderMultiplier×100×SV) × beatLength
int64_t OsuParser::estimateSliderEndTime(const RawHitObject& obj) const {
    // 对齐参考转换器 estimateSliderEndTime 公式
    double beatLength = getMsPerBeatAt(obj.time);
    double sliderVelocity = getSliderVelocityAt(obj.time);
    double denominator = std::max(0.001, m_sliderMultiplier * 100.0 * sliderVelocity);
    double beats = obj.length * std::max(1, obj.slides) / denominator;
    int64_t duration = static_cast<int64_t>(std::llround(beats * beatLength));
    return obj.time + std::max<int64_t>(1, duration);
}

// ══════════════════════════════════════════════════════
//  makeConvertedNotes — RawHitObject → ConvertedNote
//  Circle→Tap，Slider→Hold（估算 endTime），Spinner→Tap（中心）
//  同时计算击打窗口与 Hold 释放窗口（releaseWindow）
// ══════════════════════════════════════════════════════

std::vector<OsuParser::ConvertedNote> OsuParser::makeConvertedNotes() const {
    // RawHitObject → ConvertedNote（含窗口计算 + slider endTime 估算）
    std::vector<ConvertedNote> notes;
    notes.reserve(m_rawObjects.size());
    for (const auto& obj : m_rawObjects) {
        ConvertedNote n;
        n.x = obj.x;
        n.y = obj.y;
        n.time = obj.time;
        if (obj.type & 1) {
            // HitCircle → Tap
            n.type = 'T';
            n.endTime = obj.time;
        } else if (obj.type & 2) {
            // Slider → Hold
            n.type = 'H';
            n.endTime = estimateSliderEndTime(obj);
        } else if (obj.type & 4) {
            // Spinner → Tap（转盘渲染成 tap）
            n.type = 'T';
            n.endTime = obj.time;
        }
        n.window = makeWindow(n.time);
        // Hold 的释放窗口基于 endTime（对齐参考转换器 osz_to_mma.cpp:515）
        // blockingLatestHit 依赖 releaseWindow.latestHit 判断 Hold 是否已释放完毕
        n.releaseWindow = makeWindow(n.type == 'H' ? n.endTime : n.time);
        notes.push_back(n);
    }
    return notes;
}

/// ±1500ms 窗口内 note 密度 → 目标 rows×cols（2×3 ~ 4×6）
OsuParser::MatrixShape OsuParser::targetShapeForDensity(const std::vector<ConvertedNote>& notes, size_t index) const {
    // 基于 ±1500ms 窗口内 note 数量映射目标阵型
    if (notes.empty()) return {kBaseRows, kBaseCols};
    const int64_t window = 1500;
    int64_t t = notes[index].time;
    int count = 0;
    for (const auto& n : notes) {
        if (!n.dropped && n.time >= t - window && n.time <= t + window) ++count;
    }
    double dps = count / 3.0;  // density per second（窗口=3000ms）
    MatrixShape s;
    if (dps < 1.5)      s = {2, 3};
    else if (dps < 2.5) s = {3, 4};
    else if (dps < 3.0) s = {4, 5};
    else                s = {4, 6};
    return s;
}

int OsuParser::transformTypeFor(const MatrixShape& from, const MatrixShape& to) {
    // 根据行列变化计算 transformType（对齐参考转换器，使用 beatmap.h MatrixTransform 常量）
    int rowDelta = to.rows - from.rows;
    int colDelta = to.cols - from.cols;
    if (rowDelta == 0 && colDelta == 0) {
        return MatrixTransform::NONE;
    }
    if (rowDelta > 0 && colDelta == 0) {
        return rowDelta == 1 ? MatrixTransform::SLIDE_ROW_ADD_BOTTOM : MatrixTransform::SLIDE_ROW_ADD_BOTH;
    }
    if (rowDelta < 0 && colDelta == 0) {
        return (-rowDelta) == 1 ? MatrixTransform::SLIDE_ROW_REMOVE_BOTTOM : MatrixTransform::SLIDE_ROW_REMOVE_BOTH;
    }
    if (colDelta > 0 && rowDelta == 0) {
        return colDelta == 1 ? MatrixTransform::SLIDE_COL_ADD_RIGHT : MatrixTransform::SLIDE_COL_ADD_BOTH;
    }
    if (colDelta < 0 && rowDelta == 0) {
        return (-colDelta) == 1 ? MatrixTransform::SLIDE_COL_REMOVE_RIGHT : MatrixTransform::SLIDE_COL_REMOVE_BOTH;
    }
    if (rowDelta > 0 && colDelta > 0) {
        return MatrixTransform::ROTATE_ROWS_COLS_ADD;
    }
    if (rowDelta > 0 && colDelta < 0) {
        return MatrixTransform::ROTATE_ROWS_ADD_COLS_REMOVE;
    }
    if (rowDelta < 0 && colDelta > 0) {
        return MatrixTransform::ROTATE_ROWS_REMOVE_COLS_ADD;
    }
    if (rowDelta < 0 && colDelta < 0) {
        return MatrixTransform::ROTATE_ROWS_COLS_REMOVE;
    }
    return MatrixTransform::ROTATE_COMPLEX;
}

int OsuParser::previousKeptIndex(const std::vector<ConvertedNote>& notes, size_t before) {
    // 向前查找最近一个未丢弃的 note 索引
    for (size_t i = before; i > 0; --i) {
        const size_t index = i - 1;
        if (!notes[index].dropped) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int OsuParser::nextKeptIndex(const std::vector<ConvertedNote>& notes, size_t after) {
    // 向后查找最近一个未丢弃的 note 索引
    for (size_t i = after + 1; i < notes.size(); ++i) {
        if (!notes[i].dropped) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int OsuParser::mappedColForShape(int x, int cols) {
    const double clampedX = std::max(0.0, std::min(511.999, static_cast<double>(x)));
    return std::max(0, std::min(static_cast<int>(std::floor(clampedX / 512.0 * cols)), cols - 1));
}

int64_t OsuParser::blockingLatestHit(const ConvertedNote& note) {
    // 与 noteBlocksUntilMs 对齐：Tap → window.latestHit(=time+hit50)；Hold → endTime(=holdEnd)
    return note.type == 'H' ? note.endTime : note.window.latestHit;
}

bool OsuParser::downgradeHoldToTapIfSafe(ConvertedNote& note, int64_t cutoffMs) {
    if (note.type != 'H' || note.window.latestHit > cutoffMs) {
        return false;
    }

    note.type = 'T';
    note.endTime = note.time;
    note.releaseWindow = note.window;
    return true;
}

int OsuParser::blockingConflictIndexBefore(const std::vector<ConvertedNote>& notes,
                                           size_t before, int64_t cutoffMs) {
    int conflict = -1;
    int64_t latestBlock = cutoffMs;
    for (size_t i = 0; i < before; ++i) {
        if (notes[i].dropped) continue;
        const int64_t latestHit = blockingLatestHit(notes[i]);
        if (latestHit > latestBlock) {
            latestBlock = latestHit;
            conflict = static_cast<int>(i);
        }
    }
    return conflict;
}

int64_t OsuParser::maxBlockingLatestHitBefore(const std::vector<ConvertedNote>& notes, size_t before) {
    // 遍历 before 之前所有未丢弃 note，取最大的 blockingLatestHit。
    // 关键：只看 prevIndex 会漏掉更早的长 Hold（其 releaseWindow.latestHit 可能更晚），
    // 导致变换在 Hold 仍按住时就开始，引发渲染出界与击打空间丢失。
    int64_t maxLatest = 0;
    for (size_t i = 0; i < before; ++i) {
        if (notes[i].dropped) continue;
        const int64_t lh = blockingLatestHit(notes[i]);
        if (lh > maxLatest) maxLatest = lh;
    }
    return maxLatest;
}

bool OsuParser::isDenseRhythmAround(const std::vector<ConvertedNote>& notes, size_t index) const {
    // 判断当前 note 与相邻 note 的时间间隔是否小于密集判定阈值
    const int prevIndex = previousKeptIndex(notes, index);
    if (prevIndex >= 0 && notes[index].time - notes[prevIndex].time <= kDenseGapMs) {
        return true;
    }
    const int nextIndex = nextKeptIndex(notes, index);
    if (nextIndex >= 0 && notes[nextIndex].time - notes[index].time <= kDenseGapMs) {
        return true;
    }
    return false;
}

int64_t OsuParser::scrollStartMsForNote(const std::vector<ConvertedNote>& notes, size_t noteIndex,
                                        const MatrixShape& holdShape, int holdWindowStart,
                                        int64_t lastTransitionEnd) const {
    // 滚动最早开始 = max(approach 触发, 变阵结束, 窗内前向 Tap/Hold 的 noteBlocksUntil)
    const int holdWindowEnd = holdWindowStart + kActiveCols - 1;
    const int64_t targetTime = notes[noteIndex].time;
    const int64_t triggerMs = scrollTriggerMs(targetTime, lastTransitionEnd, m_ar);

    int64_t readyMs = triggerMs;
    for (size_t j = 0; j < noteIndex; ++j) {
        if (notes[j].dropped) {
            continue;
        }
        if (notes[j].time >= targetTime) {
            continue;
        }

        const int col = mappedColForShape(notes[j].x, holdShape.cols);
        if (col < holdWindowStart || col > holdWindowEnd) {
            continue;
        }

        const int64_t blockUntil = noteBlocksUntilMs(
            notes[j].time, notes[j].type == 'H', notes[j].endTime, m_od);
        if (blockUntil > triggerMs) {
            readyMs = std::max(readyMs, blockUntil);
        }
    }

    return readyMs;
}

// ══════════════════════════════════════════════════════
//  resolveScrollConflict — 解析期滚动冲突消解
//  目标列须在 nextActiveStart 窗内；scrollEnd ≤ earliestHit。
//  装不下时：降级/丢前向窗内阻塞 note（不丢目标）；仍不行则本次不滚。
// ══════════════════════════════════════════════════════

namespace {

/// 满足 S + scrollDuration(S) ≤ earliestHit 的最晚 S；无解返回 -1。
int64_t latestFeasibleScrollStartMs(int64_t targetTimeMs, int64_t earliestHitMs, float od) {
    const int64_t maxS = earliestHitMs - kScrollDurationMinMs;
    if (maxS < 0) {
        return -1;
    }
    const int64_t minS = earliestHitMs - kScrollDurationMaxMs - 1;
    for (int64_t s = maxS; s >= minS; --s) {
        if (s + scrollDurationMs(targetTimeMs, s, od) <= earliestHitMs) {
            return s;
        }
    }
    return -1;
}

}  // namespace

bool OsuParser::resolveScrollConflict(std::vector<ConvertedNote>& notes, size_t noteIndex,
                                      const MatrixShape& holdShape, int holdWindowStart,
                                      const MatrixShape& targetShape, int nextActiveStart,
                                      int64_t lastTransitionEnd, int64_t& outScrollEndMs) {
    const int targetCol = mappedColForShape(notes[noteIndex].x, targetShape.cols);
    if (targetCol < nextActiveStart || targetCol >= nextActiveStart + kActiveCols) {
        // 目标列不在新窗：不滚、不丢后向
        return false;
    }

    const int64_t targetTime = notes[noteIndex].time;
    const int64_t earliestHit = notes[noteIndex].window.earliestHit;
    const int64_t triggerMs = scrollTriggerMs(targetTime, lastTransitionEnd, m_ar);
    const int64_t latestOkStart = latestFeasibleScrollStartMs(targetTime, earliestHit, m_od);
    if (latestOkStart < 0 || triggerMs > latestOkStart) {
        // 仅 trigger 已装不下（与前向无关）：不滚、不丢目标
        return false;
    }

    const int holdWindowEnd = holdWindowStart + kActiveCols - 1;
    constexpr int kMaxForwardDrops = 5;
    int dropsLeft = kMaxForwardDrops;
    std::vector<NoteRestore> mutatedForAttempt;
    std::vector<size_t> droppedForAttempt;

    auto restoreAttempt = [&]() {
        restoreNoteMutations(notes, mutatedForAttempt);
        for (size_t index : droppedForAttempt) {
            notes[index].dropped = false;
        }
    };

    auto forwardConflictIndex = [&](int64_t cutoffMs) -> int {
        int conflict = -1;
        int64_t worstBlock = cutoffMs;
        for (size_t j = 0; j < noteIndex; ++j) {
            if (notes[j].dropped || notes[j].time >= targetTime) {
                continue;
            }
            const int col = mappedColForShape(notes[j].x, holdShape.cols);
            if (col < holdWindowStart || col > holdWindowEnd) {
                continue;
            }
            const int64_t blockUntil = noteBlocksUntilMs(
                notes[j].time, notes[j].type == 'H', notes[j].endTime, m_od);
            if (blockUntil > worstBlock) {
                worstBlock = blockUntil;
                conflict = static_cast<int>(j);
            }
        }
        return conflict;
    };

    while (true) {
        const int64_t scrollStartMs = scrollStartMsForNote(
            notes, noteIndex, holdShape, holdWindowStart, lastTransitionEnd);
        const int64_t duration = scrollDurationMs(targetTime, scrollStartMs, m_od);
        const int64_t scrollEndMs = scrollStartMs + duration;
        if (scrollStartMs <= earliestHit && scrollEndMs <= earliestHit) {
            outScrollEndMs = scrollEndMs;
            return true;
        }

        // 需要 scrollStart ≤ latestOkStart；丢掉/降级把 readyMs 推过该线的前向 note
        const int conflictIndex = forwardConflictIndex(latestOkStart);
        if (conflictIndex < 0) {
            restoreAttempt();
            return false;
        }

        auto& conflict = notes[static_cast<size_t>(conflictIndex)];
        mutatedForAttempt.push_back(NoteRestore{static_cast<size_t>(conflictIndex), conflict});
        if (downgradeHoldToTapIfSafe(conflict, latestOkStart)) {
            continue;
        }

        if (dropsLeft <= 0) {
            restoreAttempt();
            return false;
        }

        conflict.dropped = true;
        droppedForAttempt.push_back(static_cast<size_t>(conflictIndex));
        --dropsLeft;
    }
}

/// 在 note 前瞻窗内用 chooseScrollWindowStart 选 nextActiveStart
int OsuParser::chooseScrollActiveStart(const std::vector<ConvertedNote>& notes, size_t noteIndex,
                                       const MatrixShape& shape, int currentStart, int targetCol) const {
    std::vector<Note> windowNotes;
    windowNotes.reserve(notes.size() - noteIndex);
    for (size_t i = noteIndex; i < notes.size(); ++i) {
        if (notes[i].dropped) {
            continue;
        }
        Note n;
        n.time = notes[i].time;
        n.col = mappedColForShape(notes[i].x, shape.cols);
        windowNotes.push_back(n);
    }
    return chooseScrollWindowStart(currentStart, targetCol, shape.cols, kActiveCols,
                                   notes[noteIndex].time, windowNotes);
}

void OsuParser::restoreNoteMutations(std::vector<ConvertedNote>& notes,
                                      const std::vector<NoteRestore>& restore) {
    for (auto it = restore.rbegin(); it != restore.rend(); ++it) {
        notes[it->index] = it->note;
    }
}

bool OsuParser::hasStableFormationTarget(const std::vector<ConvertedNote>& notes, size_t index,
                                          const MatrixShape& current, const MatrixShape& target) const {
    // 检查前瞻窗口内是否有足够多数 note 稳定指向同一目标阵型
    if (target.rows == current.rows && target.cols == current.cols) {
        return false;
    }
    const int64_t end = notes[index].time + kFormationStabilityLookaheadMs;
    int considered = 0;
    int targetVotes = 0;
    for (size_t i = index; i < notes.size(); ++i) {
        if (notes[i].dropped) continue;
        if (notes[i].time > end) break;
        const MatrixShape candidate = targetShapeForDensity(notes, i);
        ++considered;
        if (candidate.rows == target.rows && candidate.cols == target.cols) {
            ++targetVotes;
        }
    }
    return considered >= kMinStableTargetNotes && targetVotes * 2 >= considered + 1;
}

/// 在 note 判定/显示窗口前插入变阵动画；冲突时降级 Hold→Tap 或 dropped
bool OsuParser::scheduleTransitionBefore(std::vector<ConvertedNote>& notes, size_t noteIndex,
                                           int64_t durationMs, bool includesFormation,
                                           int64_t lastTransitionEnd, int maxDrops, int64_t& startMs) const {
    // 在 note 窗口前安排过渡：循环丢弃冲突 note，失败时恢复丢弃（对齐参考转换器）
    // safeAfter 覆盖 noteIndex 之前所有 kept note 的 max(blockingLatestHit)：
    // Tap=latestHit，Hold=endTime，避免变换期间前 note 仍占列。
    const int64_t endLimit = includesFormation
        ? notes[noteIndex].window.displayStart
        : notes[noteIndex].window.earliestHit;
    std::vector<size_t> droppedForAttempt;
    std::vector<NoteRestore> mutatedForAttempt;

    while (true) {
        const int64_t latestStart = std::max<int64_t>(lastTransitionEnd, endLimit - durationMs);
        const int64_t safeAfter = maxBlockingLatestHitBefore(notes, noteIndex);
        const int64_t earliestStart = std::max(safeAfter, lastTransitionEnd);
        if (endLimit - earliestStart >= durationMs) {
            startMs = std::max(earliestStart, endLimit - durationMs);
            return true;
        }

        const int conflictIndex = blockingConflictIndexBefore(notes, noteIndex, latestStart);
        if (conflictIndex >= 0) {
            auto& conflict = notes[static_cast<size_t>(conflictIndex)];
            mutatedForAttempt.push_back(NoteRestore{static_cast<size_t>(conflictIndex), conflict});
            if (downgradeHoldToTapIfSafe(conflict, latestStart)) {
                continue;
            }

            if (maxDrops <= 0) {
                restoreNoteMutations(notes, mutatedForAttempt);
                for (size_t index : droppedForAttempt) {
                    notes[index].dropped = false;
                }
                return false;
            }

            conflict.dropped = true;
            droppedForAttempt.push_back(static_cast<size_t>(conflictIndex));
            --maxDrops;
            continue;
        }

        if (endLimit >= durationMs && lastTransitionEnd <= endLimit - durationMs) {
            const int64_t fallbackStart = std::max<int64_t>(lastTransitionEnd, endLimit - durationMs);
            std::vector<size_t> holdsToDowngrade;
            bool hasUndowngradableConflict = false;

            // Fallback：仅当冲突 Hold 可降级为 Tap 时才忽略 blocking
            for (size_t i = 0; i < noteIndex; ++i) {
                if (notes[i].dropped) continue;
                if (blockingLatestHit(notes[i]) <= fallbackStart) continue;

                if (notes[i].type == 'H' && notes[i].window.latestHit <= fallbackStart) {
                    holdsToDowngrade.push_back(i);
                    continue;
                }
                hasUndowngradableConflict = true;
                break;
            }

            if (!hasUndowngradableConflict) {
                for (size_t index : holdsToDowngrade) {
                    notes[index].type = 'T';
                    notes[index].endTime = notes[index].time;
                    notes[index].releaseWindow = notes[index].window;
                }
                startMs = fallbackStart;
                return true;
            }
        }
        if (endLimit >= durationMs && lastTransitionEnd <= endLimit - durationMs) {
            // 上一 fallback 路径不安全（绕过 safeAfter）：恢复后失败
            restoreNoteMutations(notes, mutatedForAttempt);
            for (size_t index : droppedForAttempt) {
                notes[index].dropped = false;
            }
            return false;
        }
        // 无法安排：恢复本次尝试丢弃的 note
        restoreNoteMutations(notes, mutatedForAttempt);
        for (size_t index : droppedForAttempt) {
            notes[index].dropped = false;
        }
        return false;
    }
}

// ══════════════════════════════════════════════════════
//  generateFormationsAndFilter — 动态呼吸矩阵主循环
//  逐步扫描 note：密度→目标阵型；冷却/稳定度/密集节奏门控；
//  纯滚动 vs 变阵+滚动；scheduleTransitionBefore 安排 Formation。
// ══════════════════════════════════════════════════════

std::vector<Formation> OsuParser::generateFormationsAndFilter(std::vector<ConvertedNote>& notes) {
    // 生成 formations 并过滤冲突 note（对齐参考转换器：稳定性检查 + 居中 + maxDrops + 排序）
    std::vector<Formation> formations;
    // 初始 formation
    {
        Formation f;
        f.time = 0;
        f.rows = kBaseRows;
        f.cols = kBaseCols;
        f.transformType = MatrixTransform::NONE;
        f.transformDurationMs = 0;
        f.blockSize = 1.0f;
        formations.push_back(f);
    }

    MatrixShape current{kBaseRows, kBaseCols};
    int activeStart = 0;
    int64_t lastTransitionEnd = 0;
    int64_t lastFormationTime = 0;

    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].dropped) continue;

        MatrixShape target = targetShapeForDensity(notes, i);
        const bool denseRhythm = isDenseRhythmAround(notes, i);
        const bool highDensityTarget = target.cols >= kHighDensityCols;
        bool needsFormation = target.rows != current.rows || target.cols != current.cols;
        // 高密度目标允许在密集节奏中变阵，否则 6 列段会被 denseRhythm 长时间压住。
        if (needsFormation && (
                (denseRhythm && !highDensityTarget) ||
                notes[i].time - lastFormationTime < kFormationCooldownMs ||
                !hasStableFormationTarget(notes, i, current, target))) {
            target = current;
            needsFormation = false;
        }

        // 将 activeStart 限制在当前阵型合法范围；变阵/滚动目标窗在 target 空间单独计算。
        const int currentMaxActiveStart = std::max(0, current.cols - kActiveCols);
        activeStart = std::max(0, std::min(activeStart, currentMaxActiveStart));
        notes[i].scrollWindowStart = activeStart;
        const int clampedTargetCol = mappedColForShape(notes[i].x, target.cols);
        bool needsScroll = false;
        int nextActiveStart = activeStart;
        if (needsFormation) {
            if (target.cols > kActiveCols) {
                nextActiveStart = chooseScrollActiveStart(notes, i, target, activeStart, clampedTargetCol);
            } else {
                nextActiveStart = 0;
            }
        } else if (target.cols > kActiveCols) {
            if (clampedTargetCol < activeStart) {
                needsScroll = true;
                nextActiveStart = chooseScrollActiveStart(notes, i, target, activeStart, clampedTargetCol);
            } else if (clampedTargetCol >= activeStart + kActiveCols) {
                needsScroll = true;
                nextActiveStart = chooseScrollActiveStart(notes, i, target, activeStart, clampedTargetCol);
            }
        }

        if (!needsFormation && !needsScroll) continue;

        // ── 仅滚动（不变阵）──
        if (!needsFormation && needsScroll) {
            int64_t scrollEndMs = 0;
            if (!resolveScrollConflict(
                    notes, i, current, activeStart, target, nextActiveStart,
                    lastTransitionEnd, scrollEndMs)) {
                continue;
            }
            notes[i].scrollWindowStart = nextActiveStart;
            lastTransitionEnd = scrollEndMs;
            activeStart = nextActiveStart;
            continue;
        }

        // ── 变阵：目标窗在 target 坐标空间直接选取；不再走不可达的「变阵前滚动」分支 ──
        // （needsScroll 仅在 !needsFormation 时置位，此前死代码已删除）

        // ── 安排变阵动画并写入 Formation ──
        const int64_t duration = static_cast<int64_t>(kFormationDurationMs);

        int64_t transitionStart = 0;
        const int maxDrops = 5;
        if (!scheduleTransitionBefore(
                notes, i, duration, true, lastTransitionEnd, maxDrops, transitionStart)) {
            continue;
        }

        lastTransitionEnd = transitionStart + duration;
        activeStart = nextActiveStart;
        notes[i].scrollWindowStart = nextActiveStart;
        const int type = transformTypeFor(current, target);
        Formation f;
        f.time = transitionStart;
        f.rows = target.rows;
        f.cols = target.cols;
        f.transformType = type;
        f.transformDurationMs = kFormationDurationMs;
        f.blockSize = static_cast<float>(kDefaultBlockSize);
        formations.push_back(f);
        current = target;
        lastFormationTime = transitionStart;
    }

    // 按时间排序
    std::sort(formations.begin(), formations.end(),
              [](const Formation& a, const Formation& b) { return a.time < b.time; });
    MM_LOG_INFO("OsuParser", "Generated " + std::to_string(formations.size()) + " formations");
    return formations;
}

// ── arrangeRemainingNotes 辅助：稳定列 / 边缘列判定 ──

/// 滚动窗内「稳定列」区间（滚后仍留在窗内）：如 0-3→1-3，1-4→2-3
void OsuParser::stableArrangeColRange(int totalCols, int winStart, int winEnd,
                                      int& stableStart, int& stableEnd) {
    stableStart = winStart;
    stableEnd = winEnd;
    if (totalCols <= kActiveCols || winEnd <= winStart) {
        return;
    }

    if (winStart == 0) {
        stableStart = 1;
        stableEnd = winEnd;
    } else if (winEnd == totalCols - 1) {
        stableEnd = winEnd - 1;
        stableStart = (winStart == 1) ? 2 : winStart;
    } else {
        stableStart = winStart + 1;
        stableEnd = winEnd - 1;
    }

    stableStart = std::max(winStart, std::min(stableStart, winEnd));
    stableEnd = std::max(stableStart, std::min(stableEnd, winEnd));
}

bool OsuParser::isStableArrangeCol(int col, int stableStart, int stableEnd) {
    return col >= stableStart && col <= stableEnd;
}

bool OsuParser::isScrollEdgeCol(int col, int winStart, int winEnd,
                                int stableStart, int stableEnd) {
    return col >= winStart && col <= winEnd && !isStableArrangeCol(col, stableStart, stableEnd);
}

bool OsuParser::moveAffectsScroll(int newCol, int winStart, int winEnd,
                                  int stableStart, int stableEnd,
                                  int nextWinStart, int64_t nextScrollStartMs,
                                  int64_t latestHit) {
    if (!isScrollEdgeCol(newCol, winStart, winEnd, stableStart, stableEnd)) {
        return false;
    }
    if (nextWinStart < 0) {
        return false;
    }
    const int nextWinEnd = nextWinStart + kActiveCols - 1;
    if (newCol >= nextWinStart && newCol <= nextWinEnd) {
        return false;
    }
    return nextScrollStartMs < latestHit;
}

// ══════════════════════════════════════════════════════
//  arrangeRemainingNotes — 滚动/变阵完成后的行列编排
//  按时间批次处理同拍 note：列密度超阈值时重定位；
//  稳定列优先；边缘列+后续滚动冲突时回退稳定列；
//  Hold 占列至 endTime（colHoldUntil）。
// ══════════════════════════════════════════════════════

void OsuParser::arrangeRemainingNotes(std::vector<ConvertedNote>& notes,
                                      const std::vector<Formation>& formations) const {
    struct AssignEntry {
        size_t index = 0;
        int64_t time = 0;
        char type = 'T';
        int64_t endTime = 0;
        int64_t latestHit = 0;
        int scrollWindowStart = 0;
        int32_t origRow = 0;
        int32_t origCol = 0;
        int32_t row = 0;
        int32_t col = 0;
    };

    /// 同列在 ±节奏窗内的 note 数量（含当前）；窗 = 当前 BPM 下需编排的拍子细分时长
    auto colLoadInWindow = [this](const std::vector<AssignEntry>& assigned, size_t before,
                                  int64_t time, int32_t col, int maxCols) {
        const int64_t rhythmWindowMs = arrangeRhythmWindowMs(time);
        int load = 0;
        for (size_t p = 0; p < before; ++p) {
            const auto& prev = assigned[p];
            if (prev.col != col) {
                continue;
            }
            if (std::llabs(prev.time - time) > rhythmWindowMs) {
                continue;
            }
            if (col < 0 || col >= maxCols) {
                continue;
            }
            ++load;
        }
        return load;
    };

    /// 同批次内 (row,col) 是否已被占用
    auto cellTaken = [](const std::vector<AssignEntry>& assigned, size_t batchBegin, size_t before,
                        int32_t row, int32_t col) {
        for (size_t s = batchBegin; s < before; ++s) {
            if (assigned[s].row == row && assigned[s].col == col) {
                return true;
            }
        }
        return false;
    };

    /// 收集未 dropped 的 note，按 formations 时刻 pixelToGrid 得 origRow/origCol
    std::vector<AssignEntry> entries;
    entries.reserve(notes.size());
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].dropped) {
            continue;
        }
        AssignEntry entry;
        entry.index = i;
        entry.time = notes[i].time;
        entry.type = notes[i].type;
        entry.endTime = notes[i].endTime;
        entry.latestHit = blockingLatestHit(notes[i]);  // Tap=window.latestHit；Hold=endTime
        entry.scrollWindowStart = notes[i].scrollWindowStart;
        const MatrixShape shape = shapeAtTime(formations, entry.time);
        pixelToGrid(notes[i].x, notes[i].y, shape.rows, shape.cols, entry.origRow, entry.origCol);
        entries.push_back(entry);
    }

    /// 为 entry 在 col 上选首个未占用的 row（优先 preferredRow）
    auto pickRow = [&](const AssignEntry& entry, const MatrixShape& shape, int32_t col,
                       size_t batchBegin, size_t before, int32_t preferredRow) {
        if (!cellTaken(entries, batchBegin, before, preferredRow, col)) {
            return preferredRow;
        }
        for (int r = 0; r < shape.rows; ++r) {
            if (!cellTaken(entries, batchBegin, before, static_cast<int32_t>(r), col)) {
                return static_cast<int32_t>(r);
            }
        }
        return preferredRow;
    };

    /// 稳定列区间是否全部达到密度阈值
    auto stableColsOverloaded = [&](int stableStart, int stableEnd, int maxCols, size_t before,
                                    int64_t time, int threshold) {
        for (int c = stableStart; c <= stableEnd; ++c) {
            if (colLoadInWindow(entries, before, time, static_cast<int32_t>(c), maxCols) < threshold) {
                return false;
            }
        }
        return true;
    };

    std::vector<int64_t> colHoldUntil;
    int relocatedCount = 0;

    /// 按相同 time 分批（同拍 note 一起编排）
    size_t batchBegin = 0;
    while (batchBegin < entries.size()) {
        size_t batchEnd = batchBegin + 1;
        while (batchEnd < entries.size() && entries[batchEnd].time == entries[batchBegin].time) {
            ++batchEnd;
        }

        for (size_t b = batchBegin; b < batchEnd; ++b) {
            auto& entry = entries[b];
            const MatrixShape shape = shapeAtTime(formations, entry.time);
            if (shape.cols <= 0 || shape.rows <= 0) {
                entry.row = entry.origRow;
                entry.col = entry.origCol;
                continue;
            }

            if (static_cast<int>(colHoldUntil.size()) < shape.cols) {
                colHoldUntil.assign(static_cast<size_t>(shape.cols), 0);
            }

            const int maxStart = std::max(0, shape.cols - kActiveCols);
            const int winStart = std::max(0, std::min(entry.scrollWindowStart, maxStart));
            const int winEnd = shape.cols <= kActiveCols
                ? shape.cols - 1
                : std::min(shape.cols - 1, winStart + kActiveCols - 1);
            int stableStart = winStart;
            int stableEnd = winEnd;
            if (shape.cols > kActiveCols) {
                stableArrangeColRange(shape.cols, winStart, winEnd, stableStart, stableEnd);
            }

            entry.row = entry.origRow;
            entry.col = std::max(winStart, std::min(entry.origCol, winEnd));

            int nextWinStart = -1;
            int64_t nextScrollStartMs = INT64_MAX;
            if (shape.cols > kActiveCols) {
                for (size_t j = b + 1; j < entries.size(); ++j) {
                    const MatrixShape futureShape = shapeAtTime(formations, entries[j].time);
                    if (futureShape.cols <= kActiveCols) {
                        continue;
                    }
                    if (entries[j].scrollWindowStart != winStart) {
                        nextWinStart = entries[j].scrollWindowStart;
                        const MatrixShape holdShape = futureShape;
                        // 用上一 formation 结束时刻近似 lastTransitionEnd（编排阶段无完整滚动账本）
                        int64_t approxLastEnd = 0;
                        for (const auto& f : formations) {
                            if (f.time > entries[j].time) break;
                            approxLastEnd = std::max(approxLastEnd,
                                f.time + static_cast<int64_t>(f.transformDurationMs));
                        }
                        nextScrollStartMs = scrollStartMsForNote(
                            notes, entries[j].index, holdShape, winStart, approxLastEnd);
                        break;
                    }
                }
            }

            const int32_t activeCol = entry.col;
            const int origColLoad =
                colLoadInWindow(entries, b, entry.time, activeCol, shape.cols);
            const bool origColOverloaded =
                origColLoad + 1 >= kArrangeColDensityThreshold;

            if (origColOverloaded) {
                const bool origInStable =
                    activeCol >= stableStart && activeCol <= stableEnd;
                if (shape.cols > kActiveCols && origInStable &&
                    stableColsOverloaded(stableStart, stableEnd, shape.cols, b, entry.time,
                                         kArrangeColDensityThreshold)) {
                    entry.row = pickRow(entry, shape, activeCol, batchBegin, b, entry.origRow);
                    if (entry.type == 'H') {
                        colHoldUntil[static_cast<size_t>(entry.col)] =
                            std::max(colHoldUntil[static_cast<size_t>(entry.col)], entry.endTime);
                    }
                    continue;
                }

                int32_t bestCol = activeCol;
                int bestLoad = origColLoad + 1;
                bool found = false;

                bool hasEdgeScrollConflict = false;
                if (shape.cols > kActiveCols && nextWinStart >= 0) {
                    for (int c = winStart; c <= winEnd; ++c) {
                        if (moveAffectsScroll(c, winStart, winEnd, stableStart, stableEnd,
                                              nextWinStart, nextScrollStartMs, entry.latestHit)) {
                            hasEdgeScrollConflict = true;
                            break;
                        }
                    }
                }

                auto tryCol = [&](int c) {
                    if (c < winStart || c > winEnd) {
                        return;
                    }
                    if (entry.type != 'H' &&
                        colHoldUntil[static_cast<size_t>(c)] > entry.time) {
                        return;
                    }
                    const int load =
                        colLoadInWindow(entries, b, entry.time, static_cast<int32_t>(c), shape.cols);
                    if (load >= bestLoad) {
                        return;
                    }
                    const int32_t row = pickRow(entry, shape, static_cast<int32_t>(c), batchBegin, b,
                                                entry.origRow);
                    if (cellTaken(entries, batchBegin, b, row, static_cast<int32_t>(c))) {
                        return;
                    }
                    bestLoad = load;
                    bestCol = static_cast<int32_t>(c);
                    entry.row = row;
                    found = true;
                };

                if (hasEdgeScrollConflict) {
                    for (int c = stableStart; c <= stableEnd; ++c) {
                        tryCol(c);
                    }
                } else {
                    for (int c = winStart; c <= winEnd; ++c) {
                        tryCol(c);
                    }
                }

                if (found && bestCol != activeCol) {
                    entry.col = bestCol;
                    ++relocatedCount;
                } else {
                    entry.row = pickRow(entry, shape, activeCol, batchBegin, b, entry.origRow);
                    entry.col = activeCol;
                }
            } else {
                entry.row = pickRow(entry, shape, activeCol, batchBegin, b, entry.origRow);
            }

            if (entry.type == 'H') {
                colHoldUntil[static_cast<size_t>(entry.col)] =
                    std::max(colHoldUntil[static_cast<size_t>(entry.col)], entry.endTime);
            }
        }

        batchBegin = batchEnd;
    }

    for (const auto& entry : entries) {
        notes[entry.index].gridRow = entry.row;
        notes[entry.index].gridCol = entry.col;
    }

    MM_LOG_INFO("OsuParser", "Arranged layout for " + std::to_string(entries.size()) +
                " notes, relocated " + std::to_string(relocatedCount) +
                " (rhythmSubdiv: >=140→1/3, 100-140→1/4, <100→1/6)");
}

OsuParser::MatrixShape OsuParser::shapeAtTime(const std::vector<Formation>& formations, int64_t time) {
    // 查找指定时间的阵型形状
    MatrixShape s{kBaseRows, kBaseCols};
    for (const auto& f : formations) {
        if (f.time <= time) { s.rows = f.rows; s.cols = f.cols; }
        else break;
    }
    return s;
}

// ── 主 parse：分段读取 → 转换流水线 → 写入 BeatmapBuilder ──

util::Result<void> OsuParser::parse(const std::string& content, BeatmapBuilder& builder) {
    std::istringstream stream(content);
    std::string line;

    enum class Section { None, General, Metadata, Difficulty, TimingPoints, Events, HitObjects };
    Section currentSection = Section::None;

    // 临时存储
    std::string audioFile, title, artist, creator, version;
    int64_t previewTime = 0;
    std::string backgroundFile;

    // 分段累积行
    std::vector<std::string> timingLines;
    std::vector<std::string> hitObjectLines;

    builder.setFormatVersion("osu");

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == '/') continue;

        // 段落头 [Section]
        if (line.front() == '[' && line.back() == ']') {
            // 切换段落（TimingPoints/HitObjects 等行已在上方累积）
            std::string sectionName = line.substr(1, line.size() - 2);
            if (sectionName == "General")          currentSection = Section::General;
            else if (sectionName == "Metadata")    currentSection = Section::Metadata;
            else if (sectionName == "Difficulty")   currentSection = Section::Difficulty;
            else if (sectionName == "TimingPoints") currentSection = Section::TimingPoints;
            else if (sectionName == "Events")       currentSection = Section::Events;
            else if (sectionName == "HitObjects")   currentSection = Section::HitObjects;
            else currentSection = Section::None;
            continue;
        }

        // 支持 osu! 格式的 '=' 和 ':' 两种分隔符
        auto pos = line.find('=');
        if (pos == std::string::npos) pos = line.find(':');
        std::string key, value;
        if (pos != std::string::npos) {
            key = trim(line.substr(0, pos));
            value = trim(line.substr(pos + 1));
        }

        switch (currentSection) {
        case Section::General:
            if (key == "AudioFilename") audioFile = value;
            else if (key == "PreviewTime") try { previewTime = std::stoll(value); } catch(...) {}
            else if (key == "Mode") try { m_mode = std::stoi(value); } catch(...) {}
            break;

        case Section::Metadata:
            if (key == "Title")         title = value;
            else if (key == "Artist")   artist = value;
            else if (key == "Creator")  creator = value;
            else if (key == "Version")  version = value;
            break;

        case Section::Difficulty:
            if (key == "HPDrainRate")          try { m_hp = std::stof(value); } catch(...) {}
            else if (key == "OverallDifficulty") try { m_od = std::stof(value); } catch(...) {}
            else if (key == "ApproachRate")       try { m_ar = std::stof(value); } catch(...) {}
            else if (key == "SliderMultiplier")   try { m_sliderMultiplier = std::stof(value); } catch(...) {}
            break;

        case Section::TimingPoints:
            timingLines.push_back(line);
            break;

        case Section::Events:
            // 解析背景图片: 0,0,filename,0,0
            if (line.find("0,0,") == 0 || line.find("0,0,") != std::string::npos) {
                auto evtParts = split(line, ',');
                if (evtParts.size() >= 3 && evtParts[0] == "0" && evtParts[1] == "0") {
                    // 去掉文件名两端的引号
                    backgroundFile = evtParts[2];
                    if (!backgroundFile.empty() && backgroundFile.front() == '"') {
                        backgroundFile = backgroundFile.substr(1);
                    }
                    if (!backgroundFile.empty() && backgroundFile.back() == '"') {
                        backgroundFile.pop_back();
                    }
                }
            }
            break;

        case Section::HitObjects:
            hitObjectLines.push_back(line);
            break;

        default:
            break;
        }
    }

    // ── 检查模式 ──
    if (m_mode != 0) {
        MM_LOG_WARN("OsuParser", "Unsupported osu mode: " + std::to_string(m_mode) +
                    " (only Mode=0 standard is supported)");
        // 继续解析，但结果可能不理想
    }

    // ── 解析 TimingPoints ──
    parseTimingPoints(timingLines);

    // ── 解析 HitObjects ──
    parseHitObjects(hitObjectLines);

    // ── 生成 formations 并过滤冲突 note（对齐参考转换器）──
    auto convertedNotes = makeConvertedNotes();
    auto formations = generateFormationsAndFilter(convertedNotes);
    arrangeRemainingNotes(convertedNotes, formations);

    // ── 将 ConvertedNote 转换为 Note（跳过 dropped）──
    for (const auto& cn : convertedNotes) {
        if (cn.dropped) continue;
        MatrixShape shape = shapeAtTime(formations, cn.time);
        int32_t row = 0;
        int32_t col = 0;
        if (cn.gridRow >= 0 && cn.gridCol >= 0) {
            row = cn.gridRow;
            col = cn.gridCol;
        } else {
            pixelToGrid(cn.x, cn.y, shape.rows, shape.cols, row, col);
        }
        Note note;
        note.time = cn.time;
        note.row = row;
        note.col = col;
        if (cn.type == 'H') {
            note.type = NoteType::Hold;
            note.holdEnd = cn.endTime;
        } else {
            note.type = NoteType::Tap;
        }
        builder.addNote(note);
    }

    // ── 添加 Formation 序列 ──
    builder.addFormations(formations);

    // ── 设置 Meta ──
    Meta meta;
    meta.title = title;
    meta.artist = artist;
    meta.creator = creator;
    meta.version = version;
    meta.audioFile = audioFile;
    meta.previewTime = previewTime;
    // backgroundFile 暂存（Meta 结构暂未添加 backgroundFile 字段）
    builder.setMeta(meta);

    // ── 设置 Difficulty ──
    Difficulty diff;
    diff.hp = m_hp;
    diff.od = m_od;
    diff.ar = m_ar;
    builder.setDifficulty(diff);

    MM_LOG_INFO("OsuParser", "Parse complete: " + std::to_string(m_rawObjects.size()) +
                " hit objects, " + std::to_string(formations.size()) + " formations");
    return util::success();
}

// ── 解析器工厂（同 beatmap_parser.h）──

/// 按扩展名 .mma / .osu 创建解析器；未知格式默认 MmaParser
std::unique_ptr<BeatmapParser> createParserForFile(const std::string& filename) {
    auto dotPos = filename.rfind('.');
    if (dotPos != std::string::npos) {
        std::string ext = filename.substr(dotPos + 1);
        for (auto& c : ext) c = static_cast<char>(tolower(c));

        if (ext == "mma") {
            return std::make_unique<MmaParser>();
        } else if (ext == "osu") {
            return std::make_unique<OsuParser>();
        }
    }

    MM_LOG_WARN("BeatmapParser", "Unknown file format for: " + filename + ", defaulting to MMA");
    return std::make_unique<MmaParser>();
}

} // namespace melody_matrix::beatmap
