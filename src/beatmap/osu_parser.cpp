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
    // osu! playfield: 512×384 (internal resolution)
    const float cellW = 512.0f / cols;
    const float cellH = 384.0f / rows;

    outCol = static_cast<int32_t>(std::floor(x / cellW));
    outRow = static_cast<int32_t>(std::floor(y / cellH));

    // Clamp to valid range
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

// ── Slider curve interpolation (simplified: linear between control points) ──

void OsuParser::interpolateSliderPosition(const RawHitObject& obj, float progress,
                                           int& outX, int& outY) const {
    // 构建路径点序列：起始点 + 控制点
    std::vector<std::pair<int,int>> path;
    path.push_back({obj.x, obj.y});
    for (const auto& pt : obj.curvePoints) {
        path.push_back(pt);
    }

    if (path.size() < 2) {
        outX = obj.x;
        outY = obj.y;
        return;
    }

    // 计算各段长度和总长度
    struct Segment { int x1, y1, x2, y2; double len; };
    std::vector<Segment> segments;
    double totalLen = 0.0;
    for (size_t i = 1; i < path.size(); ++i) {
        Segment seg;
        seg.x1 = path[i-1].first;  seg.y1 = path[i-1].second;
        seg.x2 = path[i].first;    seg.y2 = path[i].second;
        seg.len = std::sqrt(static_cast<double>((seg.x2-seg.x1)*(seg.x2-seg.x1) +
                                                  (seg.y2-seg.y1)*(seg.y2-seg.y1)));
        segments.push_back(seg);
        totalLen += seg.len;
    }

    if (totalLen < 1.0) {
        outX = obj.x;
        outY = obj.y;
        return;
    }

    // 沿路径插值
    double targetDist = progress * totalLen;
    double accumulated = 0.0;
    for (const auto& seg : segments) {
        if (accumulated + seg.len >= targetDist) {
            double t = (seg.len > 0.0) ? (targetDist - accumulated) / seg.len : 0.0;
            outX = static_cast<int>(seg.x1 + t * (seg.x2 - seg.x1));
            outY = static_cast<int>(seg.y1 + t * (seg.y2 - seg.y1));
            return;
        }
        accumulated += seg.len;
    }

    // 退化：返回最后一个点
    outX = path.back().first;
    outY = path.back().second;
}

// ── Dynamic Breathing Formation (non-square: rows by y-spread, cols by x-spread) ──

std::vector<Formation> OsuParser::generateBreathingFormations(
    const std::vector<RawHitObject>& objects,
    int64_t windowMs,
    int32_t minRows, int32_t maxRows,
    int32_t minCols, int32_t maxCols,
    int32_t hysteresis) const
{
    if (objects.empty()) {
        return {Formation{0, minRows, minCols}};
    }

    // ── 第一步：滑动窗口分析 Note 空间分布 ──
    // x 方向扩散 → cols，y 方向扩散 → rows（独立映射，支持非正方形）
    struct WindowInfo {
        int64_t time;
        double meanDx = 0.0;   ///< x 方向归一化扩散 [0,1]
        double meanDy = 0.0;   ///< y 方向归一化扩散 [0,1]
        int     count = 0;
    };

    std::vector<WindowInfo> windows;
    const int64_t stepMs = 500;
    const int64_t halfWin = windowMs / 2;

    int64_t startTime = objects.front().time;
    int64_t endTime = objects.back().time;

    for (int64_t t = startTime; t <= endTime + halfWin; t += stepMs) {
        double sumDx = 0.0, sumDy = 0.0;
        int count = 0;
        for (const auto& obj : objects) {
            if (obj.time < t - halfWin) continue;
            if (obj.time > t + halfWin) break;
            if (obj.type & 4) continue;  // 跳过 Spinner

            sumDx += std::abs(obj.x - 256.0) / 256.0;
            sumDy += std::abs(obj.y - 192.0) / 192.0;
            count++;
        }

        WindowInfo wi;
        wi.time = t;
        wi.count = count;
        if (count > 0) {
            wi.meanDx = sumDx / count;
            wi.meanDy = sumDy / count;
            double densityFactor = std::min(1.0, count / 8.0);
            wi.meanDx *= (0.4 + 0.6 * densityFactor);
            wi.meanDy *= (0.4 + 0.6 * densityFactor);
        }
        windows.push_back(wi);
    }

    // ── 第二步：分别映射到 rows/cols，带迟滞 ──
    struct FormationCandidate {
        int64_t time;
        int32_t rows;
        int32_t cols;
    };

    std::vector<FormationCandidate> candidates;
    int32_t curRows = minRows;
    int32_t curCols = minCols;

    for (const auto& wi : windows) {
        int32_t targetRows = minRows + static_cast<int32_t>(std::round(wi.meanDy * (maxRows - minRows)));
        int32_t targetCols = minCols + static_cast<int32_t>(std::round(wi.meanDx * (maxCols - minCols)));

        // 迟滞：差距超过阈值才切换；向大方向更敏感
        if (std::abs(targetRows - curRows) > hysteresis || (targetRows > curRows && targetRows - curRows >= 1)) {
            curRows = targetRows;
        }
        if (std::abs(targetCols - curCols) > hysteresis || (targetCols > curCols && targetCols - curCols >= 1)) {
            curCols = targetCols;
        }

        curRows = std::max(minRows, std::min(maxRows, curRows));
        curCols = std::max(minCols, std::min(maxCols, curCols));

        candidates.push_back({wi.time, curRows, curCols});
    }

    // ── 第三步：合并连续相同尺寸的时间段 ──
    std::vector<Formation> formations;
    if (candidates.empty()) {
        formations.push_back({0, minRows, minCols});
        return formations;
    }

    formations.push_back({0, candidates.front().rows, candidates.front().cols});
    for (size_t i = 1; i < candidates.size(); ++i) {
        const auto& prev = candidates[i - 1];
        const auto& curr = candidates[i];
        if (curr.rows != prev.rows || curr.cols != prev.cols) {
            formations.push_back({curr.time, curr.rows, curr.cols});
        }
    }

    // 去重
    std::vector<Formation> deduped;
    for (const auto& f : formations) {
        if (deduped.empty() || deduped.back().time != f.time) {
            deduped.push_back(f);
        } else {
            deduped.back() = f;
        }
    }

    MM_LOG_INFO("OsuParser", "Generated " + std::to_string(deduped.size()) +
                " breathing formations (range " +
                std::to_string(minRows) + "x" + std::to_string(minCols) + " ~ " +
                std::to_string(maxRows) + "x" + std::to_string(maxCols) + ")");
    return deduped;
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

    // ── 生成动态呼吸矩阵 ──
    auto formations = generateBreathingFormations(m_rawObjects);

    // ── 辅助：根据时间查找活动阵型 ──
    auto findFormationAt = [&formations](int64_t t) -> const Formation* {
        const Formation* result = nullptr;
        for (const auto& f : formations) {
            if (f.time <= t) { result = &f; } else break;
        }
        return result ? result : &formations.front();
    };

    // ── 将 HitObject 转换为 Note ──
    for (const auto& obj : m_rawObjects) {
        // Bit 0 (1): HitCircle — 单个音符，时间与对象一致，直接查找
        if (obj.type & 1) {
            const Formation* af = findFormationAt(obj.time);
            int32_t row, col;
            pixelToGrid(obj.x, obj.y, af->rows, af->cols, row, col);
            Note note;
            note.time = obj.time;
            note.row = row;
            note.col = col;
            note.type = NoteType::Tap;
            builder.addNote(note);
        }

        // Bit 1 (2): Slider → Hold（slider 长按，起点位置，长按期间渲染边缘进度条贴图）
        else if (obj.type & 2) {
            double msPerBeat = getMsPerBeatAt(obj.time);
            const TimingPoint* baseTP = getBaseTimingPoint(obj.time);
            double baseMsPerBeat = baseTP ? baseTP->msPerBeat : msPerBeat;
            double sv = m_sliderMultiplier * baseMsPerBeat / 1000.0;
            if (sv < 0.01) sv = 0.4;

            double totalDurationMs = 0.0;
            if (obj.length > 0) {
                totalDurationMs = obj.slides * obj.length / sv;
            } else {
                totalDurationMs = msPerBeat;
            }
            // 限制 slider 最小/最大时长
            totalDurationMs = std::max(100.0, std::min(totalDurationMs, 5000.0));

            const Formation* af = findFormationAt(obj.time);
            int32_t row, col;
            // 用 slider 起点位置
            pixelToGrid(obj.x, obj.y, af->rows, af->cols, row, col);

            Note note;
            note.time = obj.time;
            note.row = row;
            note.col = col;
            note.type = NoteType::Hold;
            note.holdEnd = obj.time + static_cast<int64_t>(totalDurationMs);
            builder.addNote(note);
        }

        // Bit 2 (4): Spinner → Tap（转盘渲染成 tap，放在中心位置，避免触发矩阵变换/滚动）
        else if (obj.type & 4) {
            const Formation* af = findFormationAt(obj.time);
            // 中心位置，尽量在不需要变换矩阵/滚动的区域
            int32_t centerRow = af->rows / 2;
            int32_t centerCol = af->cols / 2;
            Note note;
            note.time = obj.time;
            note.row = centerRow;
            note.col = centerCol;
            note.type = NoteType::Tap;
            builder.addNote(note);
        }
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
