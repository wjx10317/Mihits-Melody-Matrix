#include "beatmap/osu_parser.h"
#include "beatmap/beatmap_parser.h"
#include "beatmap/mma_parser.h"
#include "util/logger.h"
#include "util/error_codes.h"

#include <sstream>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace melody_matrix::beatmap {

// ── Helpers ──

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

// ── Coordinate conversion ──

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

// ── TimingPoint helpers ──

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

double OsuParser::getMsPerBeatAt(int64_t time) const {
    // 找最近的独立型 TimingPoint 作为基准
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

// ── HitObject parsing ──

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

// ── 转换辅助（对齐参考转换器 osz_to_mma.cpp）──

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

OsuParser::NoteWindow OsuParser::makeWindow(int64_t time) const {
    // 计算 note 的显示/判定时间窗口（对齐参考转换器 makeWindow）
    double approachMs = std::max(0.0, 1800.0 - m_ar * 120.0);
    double goodW = std::max(0.0, 65.0 - 2.6 * m_od);
    double missW = goodW + 50.0;
    NoteWindow w;
    w.displayStart = time - static_cast<int64_t>(approachMs);
    w.earliestHit = time - static_cast<int64_t>(missW);
    w.latestHit = time + static_cast<int64_t>(missW);
    return w;
}

int64_t OsuParser::estimateSliderEndTime(const RawHitObject& obj) const {
    // 对齐参考转换器 estimateSliderEndTime 公式
    double beatLength = getMsPerBeatAt(obj.time);
    double sliderVelocity = getSliderVelocityAt(obj.time);
    double denominator = std::max(0.001, m_sliderMultiplier * 100.0 * sliderVelocity);
    double beats = obj.length * std::max(1, obj.slides) / denominator;
    int64_t duration = static_cast<int64_t>(std::llround(beats * beatLength));
    return obj.time + std::max<int64_t>(1, duration);
}

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
        notes.push_back(n);
    }
    return notes;
}

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
    else if (dps < 4.0) s = {4, 5};
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

int64_t OsuParser::blockingLatestHit(const ConvertedNote& note) {
    // Hold 取释放窗口的 latestHit，Tap 取判定窗口的 latestHit
    return note.type == 'H' ? note.releaseWindow.latestHit : note.window.latestHit;
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

bool OsuParser::keepsRowsStableNearTransition(const std::vector<ConvertedNote>& notes, size_t index,
                                                const MatrixShape& current, const MatrixShape& target) const {
    // 检查过渡前瞻窗口内 note 的行号在变换前后是否保持稳定
    if (current.rows == target.rows) {
        return true;
    }
    const int64_t end = notes[index].time + kRowStabilityLookaheadMs;
    for (size_t i = index; i < notes.size(); ++i) {
        if (notes[i].dropped) continue;
        if (notes[i].time > end) break;
        const double clampedY = std::max(0.0, std::min(383.999, static_cast<double>(notes[i].y)));
        int rowBefore = static_cast<int>(std::floor(clampedY / 384.0 * current.rows));
        int rowAfter = static_cast<int>(std::floor(clampedY / 384.0 * target.rows));
        rowBefore = std::max(0, std::min(rowBefore, current.rows - 1));
        rowAfter = std::max(0, std::min(rowAfter, target.rows - 1));
        if (rowBefore != rowAfter) {
            return false;
        }
    }
    return true;
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
        if (isDenseRhythmAround(notes, i)) {
            return false;
        }
        const MatrixShape candidate = targetShapeForDensity(notes, i);
        ++considered;
        if (candidate.rows == target.rows && candidate.cols == target.cols) {
            ++targetVotes;
        }
    }
    return considered >= kMinStableTargetNotes && targetVotes * 2 >= considered + 1;
}

bool OsuParser::scheduleTransitionBefore(std::vector<ConvertedNote>& notes, size_t noteIndex,
                                           int64_t durationMs, bool includesFormation,
                                           int64_t lastTransitionEnd, int maxDrops, int64_t& startMs) const {
    // 在 note 窗口前安排过渡：循环丢弃冲突 note，失败时恢复丢弃（对齐参考转换器）
    const int64_t endLimit = includesFormation
        ? notes[noteIndex].window.displayStart
        : notes[noteIndex].window.earliestHit;
    std::vector<size_t> droppedForAttempt;

    while (true) {
        const int prevIndex = previousKeptIndex(notes, noteIndex);
        const int64_t safeAfter = prevIndex >= 0 ? blockingLatestHit(notes[prevIndex]) : 0;
        const int64_t earliestStart = std::max(safeAfter, lastTransitionEnd);
        if (endLimit - earliestStart >= durationMs) {
            startMs = std::max(earliestStart, endLimit - durationMs);
            return true;
        }
        if (prevIndex >= 0 &&
            maxDrops > 0 &&
            !isDenseRhythmAround(notes, static_cast<size_t>(prevIndex))) {
            notes[prevIndex].dropped = true;
            droppedForAttempt.push_back(static_cast<size_t>(prevIndex));
            --maxDrops;
            continue;
        }
        if (endLimit >= durationMs && lastTransitionEnd <= endLimit - durationMs) {
            startMs = std::max<int64_t>(lastTransitionEnd, endLimit - durationMs);
            return true;
        }
        // 无法安排：恢复本次尝试丢弃的 note
        for (size_t index : droppedForAttempt) {
            notes[index].dropped = false;
        }
        return false;
    }
}

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
        bool needsFormation = target.rows != current.rows || target.cols != current.cols;
        // 密集节奏/冷却期/不稳定阵型时放弃变换
        if (needsFormation && (
                denseRhythm ||
                notes[i].time - lastFormationTime < kFormationCooldownMs ||
                !hasStableFormationTarget(notes, i, current, target))) {
            target = current;
            needsFormation = false;
        }

        // 居中：将 activeStart 限制在合法范围内，变换时取目标列中点
        const int maxActiveStart = std::max(0, target.cols - kActiveCols);
        activeStart = std::max(0, std::min(activeStart, maxActiveStart));
        const int targetCol = static_cast<int>(std::floor(std::max(0.0, std::min(511.999, static_cast<double>(notes[i].x))) / 512.0 * target.cols));
        const int clampedTargetCol = std::max(0, std::min(targetCol, target.cols - 1));
        bool needsScroll = false;
        int nextActiveStart = activeStart;
        if (needsFormation) {
            if (target.cols > kActiveCols) {
                nextActiveStart = std::max(0, std::min(clampedTargetCol - kActiveCols / 2, maxActiveStart));
            } else {
                nextActiveStart = 0;
            }
        } else if (!denseRhythm && target.cols > kActiveCols) {
            if (clampedTargetCol < activeStart) {
                needsScroll = true;
                nextActiveStart = std::max(0, std::min(clampedTargetCol, maxActiveStart));
            } else if (clampedTargetCol >= activeStart + kActiveCols) {
                needsScroll = true;
                nextActiveStart = std::max(0, std::min(clampedTargetCol - kActiveCols + 1, maxActiveStart));
            }
        }

        if (!needsFormation && !needsScroll) continue;

        const int64_t duration = std::max(
            needsFormation ? static_cast<int64_t>(kFormationDurationMs) : 0,
            needsScroll ? static_cast<int64_t>(kScrollDurationMs) : 0);

        int64_t transitionStart = 0;
        // 矩阵变换最多丢弃 5 个前向 note（也允许丢弃 Hold），滚动不丢弃 note
        const int maxDrops = needsFormation ? 5 : 0;
        if (!scheduleTransitionBefore(
                notes, i, duration, needsFormation, lastTransitionEnd, maxDrops, transitionStart)) {
            continue;
        }

        lastTransitionEnd = transitionStart + duration;
        activeStart = nextActiveStart;
        if (needsFormation) {
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
    }

    // 按时间排序
    std::sort(formations.begin(), formations.end(),
              [](const Formation& a, const Formation& b) { return a.time < b.time; });
    MM_LOG_INFO("OsuParser", "Generated " + std::to_string(formations.size()) + " formations");
    return formations;
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

// ── Main parse ──

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

        // Section header
        if (line.front() == '[' && line.back() == ']') {
            // 先刷出之前的段落
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

    // ── 将 ConvertedNote 转换为 Note（跳过 dropped）──
    for (const auto& cn : convertedNotes) {
        if (cn.dropped) continue;
        MatrixShape shape = shapeAtTime(formations, cn.time);
        int32_t row, col;
        pixelToGrid(cn.x, cn.y, shape.rows, shape.cols, row, col);
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

// ── Factory function ──

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
