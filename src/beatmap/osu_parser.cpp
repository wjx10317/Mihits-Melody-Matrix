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
//  流水线（闭环）：
//    1. parse TimingPoints/HitObjects
//    2. makeConvertedNotes（Tap/Hold + makeWindow）
//    3. generateFormationsAndFilter（变阵 + 初轮滚动；列=osu x 或已有 gridCol）
//    4. arrangeRemainingNotes（≤1/4拍换列；换行: <½appr必换 / ½~1appr随机）
//    5. resimulateScrollAfterArrange（最终 gridCol 重模拟滚动，与 runtime 同公式）
//    6. 写入 BeatmapBuilder
//
//  阻塞语义（noteBlocksUntilMs）：Tap=time+hit50；Hold=holdEnd
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

/// 编排换列间距：基拍的 1/4（不含 SV，避免继承 TP 扭曲节奏间距）。
int64_t OsuParser::arrangeQuarterBeatMs(int64_t timeMs) const {
    const TimingPoint* base = getBaseTimingPoint(timeMs);
    const double msPerBeat = base ? base->msPerBeat : 60000.0 / 120.0;
    return std::max<int64_t>(1, static_cast<int64_t>(std::llround(msPerBeat / 4.0)));
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
    // blockingLatestHit / noteBlocksUntilMs：Tap=time+hit50，Hold=endTime
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

int OsuParser::noteColForShape(const ConvertedNote& note, int cols) {
    if (cols <= 0) {
        return 0;
    }
    if (note.gridCol >= 0 && note.gridCol < cols) {
        return note.gridCol;
    }
    return mappedColForShape(note.x, cols);
}

std::vector<Note> OsuParser::buildRuntimeNotes(const std::vector<ConvertedNote>& notes,
                                               const std::vector<Formation>& formations) {
    std::vector<Note> out;
    out.reserve(notes.size());
    for (const auto& cn : notes) {
        if (cn.dropped) {
            continue;
        }
        const MatrixShape shape = shapeAtTime(formations, cn.time);
        Note n;
        n.time = cn.time;
        n.row = (cn.gridRow >= 0) ? cn.gridRow : 0;
        n.col = noteColForShape(cn, shape.cols);
        if (cn.type == 'H') {
            n.type = NoteType::Hold;
            n.holdEnd = cn.endTime;
        } else {
            n.type = NoteType::Tap;
        }
        out.push_back(n);
    }
    return out;
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

        const int col = noteColForShape(notes[j], holdShape.cols);
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
    const int targetCol = noteColForShape(notes[noteIndex], targetShape.cols);
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
            const int col = noteColForShape(notes[j], holdShape.cols);
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
        n.col = noteColForShape(notes[i], shape.cols);
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
        const int clampedTargetCol = noteColForShape(notes[i], target.cols);
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

int OsuParser::arrangeColDanger(int col, int winStart, int winEnd, int /*totalCols*/,
                                int stableStart, int stableEnd) {
    if (col < winStart || col > winEnd) {
        return 99;
    }
    // 硬边缘危险度 2；靠里「软边缘」不再额外加分（编排后会 resimulate，影响已缩小）
    if (isStableArrangeCol(col, stableStart, stableEnd)) {
        return 0;
    }
    return 2;
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
//  阶段 1（换列）：同列 note 时间间距 ≤ 1/4 拍 → 改列
//           （边缘危险度软惩罚 + Hold 占列）
//  阶段 2（换行）：同列同行 |Δt|<½approach 必换；½~1approach 随机换
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

    /// Tap=[time,time]；Hold=[time,endTime]
    auto spanOf = [](const AssignEntry& e) -> std::pair<int64_t, int64_t> {
        if (e.type == 'H') {
            return {e.time, e.endTime};
        }
        return {e.time, e.time};
    };

    auto spansOverlap = [](int64_t a0, int64_t a1, int64_t b0, int64_t b1) {
        return a0 <= b1 && b0 <= a1;
    };

    /// 同列同行视觉冲突：|Δt|<½approach / Hold 时间轴相交 → Hard；
    /// ½approach≤|Δt|<approach → Soft（随机是否换行）。
    enum class RowConflict { None = 0, Soft = 1, Hard = 2 };
    const int64_t approachLimitMs = approachMs(m_ar);
    const int64_t halfApproachMs = std::max<int64_t>(1, approachLimitMs / 2);

    auto rowConflictWithEarlier = [&](const std::vector<AssignEntry>& assigned, size_t before,
                                      const AssignEntry& cur, int32_t row, int32_t col) -> RowConflict {
        RowConflict worst = RowConflict::None;
        const auto [c0, c1] = spanOf(cur);
        for (size_t p = 0; p < before; ++p) {
            if (assigned[p].col != col || assigned[p].row != row) {
                continue;
            }
            const auto [p0, p1] = spanOf(assigned[p]);
            // Hold 盖住 / 同时刻同格 → 硬冲突
            if (spansOverlap(c0, c1, p0, p1)) {
                worst = RowConflict::Hard;
                continue;
            }
            const int64_t dt = std::llabs(assigned[p].time - cur.time);
            if (dt < halfApproachMs) {
                worst = RowConflict::Hard;
            } else if (dt < approachLimitMs && worst != RowConflict::Hard) {
                worst = RowConflict::Soft;
            }
        }
        return worst;
    };

    /// Soft 冲突是否换行：由 time/index/col 决定，转换可复现（约 50%）
    auto softShouldRelocate = [](const AssignEntry& e) {
        uint64_t x = static_cast<uint64_t>(e.time);
        x ^= static_cast<uint64_t>(e.index + 1) * 0x9E3779B97F4A7C15ULL;
        x ^= static_cast<uint64_t>(static_cast<uint32_t>(e.col) + 1) * 0xBF58476D1CE4E5B9ULL;
        x ^= x >> 33;
        return (x & 1ULL) != 0;
    };

    /// 同列已落点、时间间距 ≤ 1/4 拍的前向 note 数（选列代价）
    auto closeCountInCol = [this](const std::vector<AssignEntry>& assigned, size_t before,
                                  int64_t time, int32_t col) {
        const int64_t gapLimit = arrangeQuarterBeatMs(time);
        int count = 0;
        for (size_t p = 0; p < before; ++p) {
            if (assigned[p].col != col) {
                continue;
            }
            if (std::llabs(assigned[p].time - time) <= gapLimit + 1) {
                ++count;
            }
        }
        return count;
    };

    /// 同源列（origCol）链：前向 ≤1/4 拍则视为密串，须换列（避免第 3 个因距第 1 个已 >1/4 而留原列）
    auto chainDenseFromOrig = [this](const std::vector<AssignEntry>& assigned, size_t before,
                                     int64_t time, int32_t origCol) {
        const int64_t gapLimit = arrangeQuarterBeatMs(time);
        for (size_t p = 0; p < before; ++p) {
            if (assigned[p].origCol != origCol) {
                continue;
            }
            if (std::llabs(assigned[p].time - time) <= gapLimit + 1) {
                return true;
            }
        }
        return false;
    };

    /// 同源列已分配到 candidate 的次数（负载均衡，避免总挤同一「最近空闲列」）
    auto streamLoadOnCol = [](const std::vector<AssignEntry>& assigned, size_t before,
                              int32_t origCol, int32_t col) {
        int count = 0;
        for (size_t p = 0; p < before; ++p) {
            if (assigned[p].origCol == origCol && assigned[p].col == col) {
                ++count;
            }
        }
        return count;
    };

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
        entry.latestHit = blockingLatestHit(notes[i]);
        entry.scrollWindowStart = notes[i].scrollWindowStart;
        const MatrixShape shape = shapeAtTime(formations, entry.time);
        pixelToGrid(notes[i].x, notes[i].y, shape.rows, shape.cols, entry.origRow, entry.origCol);
        entries.push_back(entry);
    }

    std::vector<int64_t> colHoldUntil;
    int relocatedCount = 0;

    // ── 阶段 1：只定列（行暂用 origRow）──
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
                        int64_t approxLastEnd = 0;
                        for (const auto& f : formations) {
                            if (f.time > entries[j].time) {
                                break;
                            }
                            approxLastEnd = std::max(approxLastEnd,
                                f.time + static_cast<int64_t>(f.transformDurationMs));
                        }
                        nextScrollStartMs = scrollStartMsForNote(
                            notes, entries[j].index, futureShape, winStart, approxLastEnd);
                        break;
                    }
                }
            }

            const int32_t activeCol = entry.col;
            const int32_t chainOrig = entry.origCol;
            const bool needsRelocate =
                closeCountInCol(entries, b, entry.time, activeCol) > 0 ||
                chainDenseFromOrig(entries, b, entry.time, chainOrig);

            // ≤1/4 拍密串（同源链或已落点近邻）→ 换列；选列：近邻 → 同源负载 → 危险度 → 距原列
            if (needsRelocate) {
                int32_t bestCol = activeCol;
                int bestClose = closeCountInCol(entries, b, entry.time, activeCol);
                int bestLoad = streamLoadOnCol(entries, b, chainOrig, activeCol);
                int bestDanger = arrangeColDanger(activeCol, winStart, winEnd, shape.cols,
                                                  stableStart, stableEnd);
                int bestDist = std::abs(activeCol - chainOrig);

                auto tryCol = [&](int c) {
                    if (c < winStart || c > winEnd) {
                        return;
                    }
                    if (entry.type != 'H' &&
                        colHoldUntil[static_cast<size_t>(c)] > entry.time) {
                        return;
                    }
                    for (size_t s = batchBegin; s < b; ++s) {
                        if (entries[s].col == c) {
                            return;
                        }
                    }
                    const int close = closeCountInCol(entries, b, entry.time, static_cast<int32_t>(c));
                    const int load = streamLoadOnCol(entries, b, chainOrig, static_cast<int32_t>(c));
                    int danger = arrangeColDanger(c, winStart, winEnd, shape.cols,
                                                  stableStart, stableEnd);
                    if (shape.cols > kActiveCols &&
                        moveAffectsScroll(c, winStart, winEnd, stableStart, stableEnd,
                                          nextWinStart, nextScrollStartMs, entry.latestHit)) {
                        danger += 1;  // 轻惩罚（resimulate 已兜底）
                    }
                    const int dist = std::abs(c - chainOrig);

                    const bool better =
                        close < bestClose ||
                        (close == bestClose && load < bestLoad) ||
                        (close == bestClose && load == bestLoad && danger < bestDanger) ||
                        (close == bestClose && load == bestLoad && danger == bestDanger &&
                         dist < bestDist);
                    if (!better) {
                        return;
                    }
                    bestClose = close;
                    bestLoad = load;
                    bestDanger = danger;
                    bestDist = dist;
                    bestCol = static_cast<int32_t>(c);
                };

                for (int c = winStart; c <= winEnd; ++c) {
                    tryCol(c);
                }

                if (bestCol != activeCol) {
                    entry.col = bestCol;
                    ++relocatedCount;
                }
            } else if (entry.type != 'H' &&
                       colHoldUntil[static_cast<size_t>(entry.col)] > entry.time) {
                // 无近邻但仍踩在未结束 Hold 上 → 找最近可用列
                int32_t bestCol = entry.col;
                int bestDanger = 99;
                bool found = false;
                for (int c = winStart; c <= winEnd; ++c) {
                    if (colHoldUntil[static_cast<size_t>(c)] > entry.time) {
                        continue;
                    }
                    if (shape.cols > kActiveCols &&
                        moveAffectsScroll(c, winStart, winEnd, stableStart, stableEnd,
                                          nextWinStart, nextScrollStartMs, entry.latestHit)) {
                        continue;
                    }
                    const int danger = arrangeColDanger(c, winStart, winEnd, shape.cols,
                                                        stableStart, stableEnd);
                    if (!found || danger < bestDanger) {
                        found = true;
                        bestDanger = danger;
                        bestCol = static_cast<int32_t>(c);
                    }
                }
                if (found && bestCol != entry.col) {
                    entry.col = bestCol;
                    ++relocatedCount;
                }
            }

            if (entry.type == 'H') {
                colHoldUntil[static_cast<size_t>(entry.col)] =
                    std::max(colHoldUntil[static_cast<size_t>(entry.col)], entry.endTime);
            }
        }

        batchBegin = batchEnd;
    }

    // ── 阶段 2：列已定，整表换行（可见窗冲突；不在 resimulate 后再跑）──
    // Hard：|Δt|<½approach 或 Hold 时间轴相交 → 必换（行满可失败）
    // Soft：½approach≤|Δt|<approach → 随机换（保留部分叠读视觉）
    int rowFixCount = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& entry = entries[i];
        const MatrixShape shape = shapeAtTime(formations, entry.time);
        if (shape.rows <= 0) {
            continue;
        }

        entry.row = std::max(0, std::min(entry.origRow, shape.rows - 1));
        const RowConflict prefConflict =
            rowConflictWithEarlier(entries, i, entry, entry.row, entry.col);
        if (prefConflict == RowConflict::None) {
            continue;
        }
        if (prefConflict == RowConflict::Soft && !softShouldRelocate(entry)) {
            continue;
        }

        const bool requireHardFree = (prefConflict == RowConflict::Hard);
        const int32_t preferred = entry.row;

        auto rowScore = [&](int32_t row) -> int {
            const RowConflict c = rowConflictWithEarlier(entries, i, entry, row, entry.col);
            if (c == RowConflict::Hard) {
                return 1000;
            }
            if (c == RowConflict::Soft) {
                return 1;
            }
            return 0;
        };

        int32_t bestRow = preferred;
        int bestScore = rowScore(preferred);
        for (int r = 0; r < shape.rows; ++r) {
            const int32_t row = static_cast<int32_t>(r);
            const int score = rowScore(row);
            if (score < bestScore || (score == bestScore && row == preferred)) {
                bestScore = score;
                bestRow = row;
            }
        }

        // Soft 触发：只接受完全无冲突的行，否则保持叠读
        if (!requireHardFree && bestScore != 0) {
            continue;
        }
        // Hard 触发：至少要躲开 Hard；全满则保留
        if (requireHardFree && bestScore >= 1000) {
            continue;
        }

        if (bestRow != preferred) {
            entry.row = bestRow;
            ++rowFixCount;
        }
    }

    for (const auto& entry : entries) {
        notes[entry.index].gridRow = entry.row;
        notes[entry.index].gridCol = entry.col;
    }

    MM_LOG_INFO("OsuParser", "Arranged layout for " + std::to_string(entries.size()) +
                " notes, relocatedCols " + std::to_string(relocatedCount) +
                ", fixedRows " + std::to_string(rowFixCount) +
                " (colGap: <=1/4 beat; row: hard<1/2appr, soft random 1/2..1appr)");
}

// ══════════════════════════════════════════════════════
//  resimulateScrollAfterArrange — 闭环密封
//  用最终 gridCol 按 PlayingState 同款公式重走滚动：
//    变阵 → chooseScrollWindowStart 吸附；
//    出窗 → 可达则开滚（不够则丢/降前向）；不可达则夹入当前窗。
// ══════════════════════════════════════════════════════

void OsuParser::resimulateScrollAfterArrange(std::vector<ConvertedNote>& notes,
                                             const std::vector<Formation>& formations) const {
    MatrixShape current{kBaseRows, kBaseCols};
    int activeStart = 0;
    int64_t lastTransitionEnd = 0;
    size_t formationIndex = 0;
    if (!formations.empty()) {
        current = MatrixShape{formations[0].rows, formations[0].cols};
        formationIndex = 1;
    }

    int scrollCommits = 0;
    int forwardDrops = 0;
    int clampedNotes = 0;

    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].dropped) {
            continue;
        }

        // 套用已到时刻的变阵，并按 runtime snap 吸附窗口
        while (formationIndex < formations.size() &&
               formations[formationIndex].time <= notes[i].time) {
            const Formation& f = formations[formationIndex];
            current = MatrixShape{f.rows, f.cols};
            lastTransitionEnd = std::max(
                lastTransitionEnd, f.time + static_cast<int64_t>(f.transformDurationMs));

            if (current.cols <= kActiveCols) {
                activeStart = 0;
            } else {
                size_t anchor = i;
                for (size_t a = 0; a < notes.size(); ++a) {
                    if (!notes[a].dropped && notes[a].time >= f.time) {
                        anchor = a;
                        break;
                    }
                }
                if (notes[anchor].gridCol < 0 || notes[anchor].gridRow < 0) {
                    pixelToGrid(notes[anchor].x, notes[anchor].y, current.rows, current.cols,
                                notes[anchor].gridRow, notes[anchor].gridCol);
                }
                const auto runtimeNotes = buildRuntimeNotes(notes, formations);
                activeStart = chooseScrollWindowStart(
                    activeStart, noteColForShape(notes[anchor], current.cols),
                    current.cols, kActiveCols, notes[anchor].time, runtimeNotes);
            }
            ++formationIndex;
        }

        const int maxStart = std::max(0, current.cols - kActiveCols);
        activeStart = std::max(0, std::min(activeStart, maxStart));

        if (notes[i].gridCol < 0 || notes[i].gridRow < 0) {
            pixelToGrid(notes[i].x, notes[i].y, current.rows, current.cols,
                        notes[i].gridRow, notes[i].gridCol);
        }
        notes[i].gridCol = std::max(0, std::min(notes[i].gridCol, current.cols - 1));
        notes[i].gridRow = std::max(0, std::min(notes[i].gridRow, current.rows - 1));
        notes[i].scrollWindowStart = activeStart;

        if (current.cols <= kActiveCols) {
            continue;
        }

        const int col = notes[i].gridCol;
        const int winEnd = activeStart + kActiveCols - 1;
        if (col >= activeStart && col <= winEnd) {
            continue;
        }

        // 出窗：尝试开滚（与 resolveScrollConflict 同策略：丢前向不丢目标）
        const int64_t targetTime = notes[i].time;
        const int64_t earliestHit = notes[i].window.earliestHit;
        constexpr int kMaxForwardDrops = 5;
        int dropsLeft = kMaxForwardDrops;
        std::vector<NoteRestore> mutatedForAttempt;
        std::vector<size_t> droppedForAttempt;
        bool committed = false;

        auto restoreAttempt = [&]() {
            restoreNoteMutations(notes, mutatedForAttempt);
            for (size_t index : droppedForAttempt) {
                notes[index].dropped = false;
            }
        };

        while (true) {
            const auto runtimeNotes = buildRuntimeNotes(notes, formations);
            const int nextStart = chooseScrollWindowStart(
                activeStart, col, current.cols, kActiveCols, targetTime, runtimeNotes);
            if (col < nextStart || col >= nextStart + kActiveCols) {
                break;
            }

            const int64_t triggerMs = scrollTriggerMs(targetTime, lastTransitionEnd, m_ar);
            const int64_t startMs = scrollStartMsFromWindowNotes(
                activeStart, winEnd, targetTime, triggerMs, m_od, runtimeNotes);
            const int64_t endMs = startMs + scrollDurationMs(targetTime, startMs, m_od);
            if (startMs <= earliestHit && endMs <= earliestHit) {
                activeStart = nextStart;
                lastTransitionEnd = endMs;
                notes[i].scrollWindowStart = activeStart;
                committed = true;
                ++scrollCommits;
                break;
            }

            const int64_t latestOk = latestFeasibleScrollStartMs(targetTime, earliestHit, m_od);
            if (latestOk < 0 || triggerMs > latestOk) {
                break;
            }

            int conflict = -1;
            int64_t worstBlock = latestOk;
            for (size_t j = 0; j < i; ++j) {
                if (notes[j].dropped || notes[j].time >= targetTime) {
                    continue;
                }
                const int jc = noteColForShape(notes[j], current.cols);
                if (jc < activeStart || jc > winEnd) {
                    continue;
                }
                const int64_t blockUntil = noteBlocksUntilMs(
                    notes[j].time, notes[j].type == 'H', notes[j].endTime, m_od);
                if (blockUntil > worstBlock) {
                    worstBlock = blockUntil;
                    conflict = static_cast<int>(j);
                }
            }
            if (conflict < 0) {
                break;
            }

            auto& blocker = notes[static_cast<size_t>(conflict)];
            mutatedForAttempt.push_back(NoteRestore{static_cast<size_t>(conflict), blocker});
            if (downgradeHoldToTapIfSafe(blocker, latestOk)) {
                continue;
            }
            if (dropsLeft <= 0) {
                break;
            }
            blocker.dropped = true;
            droppedForAttempt.push_back(static_cast<size_t>(conflict));
            --dropsLeft;
        }

        if (committed) {
            forwardDrops += static_cast<int>(droppedForAttempt.size());
            continue;
        }

        restoreAttempt();

        // 不可达：夹入当前窗——优先避开 ≤1/4 拍近邻，再比危险度/距离
        int stableStart = activeStart;
        int stableEnd = winEnd;
        stableArrangeColRange(current.cols, activeStart, winEnd, stableStart, stableEnd);

        auto closeOnCol = [&](int candidateCol) {
            const int64_t gapLimit = arrangeQuarterBeatMs(targetTime);
            int count = 0;
            for (size_t j = 0; j < i; ++j) {
                if (notes[j].dropped || notes[j].gridCol != candidateCol) {
                    continue;
                }
                if (std::llabs(notes[j].time - targetTime) <= gapLimit + 1) {
                    ++count;
                }
            }
            return count;
        };

        int bestCol = activeStart;
        int bestClose = closeOnCol(activeStart);
        int bestDanger = arrangeColDanger(activeStart, activeStart, winEnd, current.cols,
                                          stableStart, stableEnd);
        int bestDist = std::abs(activeStart - col);
        for (int c = activeStart; c <= winEnd; ++c) {
            const int close = closeOnCol(c);
            const int danger = arrangeColDanger(c, activeStart, winEnd, current.cols,
                                                stableStart, stableEnd);
            const int dist = std::abs(c - col);
            if (close < bestClose ||
                (close == bestClose && danger < bestDanger) ||
                (close == bestClose && danger == bestDanger && dist < bestDist)) {
                bestClose = close;
                bestDanger = danger;
                bestDist = dist;
                bestCol = c;
            }
        }
        if (notes[i].gridCol != bestCol) {
            notes[i].gridCol = bestCol;
            ++clampedNotes;
        }
        notes[i].scrollWindowStart = activeStart;
    }

    // 最终窗内再疏散一遍：≤1/4 拍同列近邻尽量换到窗内更空的列（不改变 scroll 窗）
    int spreadFixes = 0;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].dropped) {
            continue;
        }
        const MatrixShape shape = shapeAtTime(formations, notes[i].time);
        if (shape.cols <= 0) {
            continue;
        }
        const int maxStart = std::max(0, shape.cols - kActiveCols);
        const int winStart = std::max(0, std::min(notes[i].scrollWindowStart, maxStart));
        const int winEnd = shape.cols <= kActiveCols
            ? shape.cols - 1
            : std::min(shape.cols - 1, winStart + kActiveCols - 1);
        int stableStart = winStart;
        int stableEnd = winEnd;
        if (shape.cols > kActiveCols) {
            stableArrangeColRange(shape.cols, winStart, winEnd, stableStart, stableEnd);
        }

        auto closeOnCol = [&](int candidateCol) {
            const int64_t gapLimit = arrangeQuarterBeatMs(notes[i].time);
            int count = 0;
            for (size_t j = 0; j < i; ++j) {
                if (notes[j].dropped || notes[j].gridCol != candidateCol) {
                    continue;
                }
                if (std::llabs(notes[j].time - notes[i].time) <= gapLimit + 1) {
                    ++count;
                }
            }
            return count;
        };

        const int curCol = notes[i].gridCol;
        const int curClose = closeOnCol(curCol);
        if (curClose <= 0) {
            continue;
        }

        int bestCol = curCol;
        int bestClose = curClose;
        int bestDanger = arrangeColDanger(curCol, winStart, winEnd, shape.cols,
                                          stableStart, stableEnd);
        for (int c = winStart; c <= winEnd; ++c) {
            // 避开未结束的前向 Hold 占列
            bool holdBlocked = false;
            for (size_t j = 0; j < i; ++j) {
                if (notes[j].dropped || notes[j].type != 'H') {
                    continue;
                }
                if (notes[j].gridCol == c && notes[j].endTime > notes[i].time) {
                    holdBlocked = true;
                    break;
                }
            }
            if (holdBlocked) {
                continue;
            }
            const int close = closeOnCol(c);
            const int danger = arrangeColDanger(c, winStart, winEnd, shape.cols,
                                                stableStart, stableEnd);
            if (close < bestClose || (close == bestClose && danger < bestDanger)) {
                bestClose = close;
                bestDanger = danger;
                bestCol = c;
            }
        }
        if (bestCol != curCol) {
            notes[i].gridCol = bestCol;
            ++spreadFixes;
        }
    }

    MM_LOG_INFO("OsuParser",
                "Resimulated scroll on final cols: commits=" + std::to_string(scrollCommits) +
                    " forwardDrops=" + std::to_string(forwardDrops) +
                    " clamped=" + std::to_string(clampedNotes) +
                    " spreadFixes=" + std::to_string(spreadFixes));
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
    resimulateScrollAfterArrange(convertedNotes, formations);

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
