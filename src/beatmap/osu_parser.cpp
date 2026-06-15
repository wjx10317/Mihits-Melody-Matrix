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

// ── Dynamic Breathing Formation ──

std::vector<Formation> OsuParser::generateBreathingFormations(
    const std::vector<RawHitObject>& objects,
    int64_t windowMs,
    int32_t minSize,
    int32_t maxSize,
    int32_t hysteresis) const
{
    if (objects.empty()) {
        return {Formation{0, minSize, minSize}};
    }

    // ── 第一步：滑动窗口分析 Note 空间分布 ──
    // 计算每个时间窗口内 Note 的边缘分布指标
    //
    // 边缘分布指标 = Note 到 playfield 中心的归一化距离的加权平均
    // playfield 中心 = (256, 192)
    // 归一化：x ∈ [0, 512] → dx = |x-256|/256, y ∈ [0, 384] → dy = |y-192|/192
    // spread = max(mean_dx, mean_dy) → 范围 [0, 1]
    // 0 = 全在中心, 1 = 全在边缘

    struct WindowInfo {
        int64_t time;        ///< 窗口中心时间
        double  spread;      ///< 边缘分布指标 [0, 1]
        int     count;       ///< 窗口内 Note 数量
    };

    std::vector<WindowInfo> windows;
    const int64_t stepMs = 500;   ///< 窗口步进（ms）
    const int64_t halfWin = windowMs / 2;

    // 确定时间范围
    int64_t startTime = objects.front().time;
    int64_t endTime = objects.back().time;

    for (int64_t t = startTime; t <= endTime + halfWin; t += stepMs) {
        // 收集窗口内的 Note
        double sumDx = 0.0, sumDy = 0.0;
        int count = 0;
        for (const auto& obj : objects) {
            if (obj.time < t - halfWin) continue;
            if (obj.time > t + halfWin) break;
            // 跳过 Spinner（它们没有有意义的位置）
            if (obj.type & 4) continue;

            double dx = std::abs(obj.x - 256.0) / 256.0;  // [0, 1]
            double dy = std::abs(obj.y - 192.0) / 192.0;  // [0, 1]
            sumDx += dx;
            sumDy += dy;
            count++;
        }

        WindowInfo wi;
        wi.time = t;
        wi.count = count;
        if (count > 0) {
            // spread = 最大归一化方向上的均值
            // 密度加权：Note 越多，分布越有意义
            double meanDx = sumDx / count;
            double meanDy = sumDy / count;
            wi.spread = std::max(meanDx, meanDy);
            // 密度加成：Note 多时 spread 的影响更显著
            double densityFactor = std::min(1.0, count / 8.0); // 8个Note以上为满密度
            wi.spread = wi.spread * (0.4 + 0.6 * densityFactor); // 稀疏时缩小 spread
        } else {
            wi.spread = 0.0;
        }

        windows.push_back(wi);
    }

    // ── 第二步：将 spread 映射到网格尺寸 ──
    // spread=0 → minSize (e.g. 3×3)
    // spread=1 → maxSize (e.g. 8×8)
    // 使用迟滞避免频繁切换

    struct FormationCandidate {
        int64_t time;
        int32_t rows;
        int32_t cols;
    };

    std::vector<FormationCandidate> candidates;
    int32_t currentSize = minSize;  // 当前网格尺寸（正方形）

    for (const auto& wi : windows) {
        // 目标尺寸：线性映射 spread → size
        int32_t targetSize = minSize + static_cast<int32_t>(
            std::round(wi.spread * (maxSize - minSize)));

        // 迟滞：只有当目标尺寸与当前尺寸差距超过 hysteresis 时才切换
        if (std::abs(targetSize - currentSize) > hysteresis) {
            currentSize = targetSize;
        }
        // 向大尺寸方向的迟滞更小（宁可多扩展不留死角）
        else if (targetSize > currentSize && (targetSize - currentSize) >= 1) {
            currentSize = targetSize;
        }

        // 限制范围
        currentSize = std::max(minSize, std::min(maxSize, currentSize));

        FormationCandidate fc;
        fc.time = wi.time;
        fc.rows = currentSize;
        fc.cols = currentSize; // 正方形网格
        candidates.push_back(fc);
    }

    // ── 第三步：合并连续相同尺寸的时间段 ──
    std::vector<Formation> formations;
    if (candidates.empty()) {
        formations.push_back({0, minSize, minSize});
        return formations;
    }

    // 初始 Formation（第一个 Note 之前）
    formations.push_back({0, candidates.front().rows, candidates.front().cols});

    for (size_t i = 1; i < candidates.size(); ++i) {
        const auto& prev = candidates[i - 1];
        const auto& curr = candidates[i];
        if (curr.rows != prev.rows || curr.cols != prev.cols) {
            formations.push_back({curr.time, curr.rows, curr.cols});
        }
    }

    // 去重（时间相同的 Formation 只保留最后一个）
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
                std::to_string(minSize) + "x" + std::to_string(minSize) + " ~ " +
                std::to_string(maxSize) + "x" + std::to_string(maxSize) + ")");

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

        // Bit 1 (2): Slider → 等间距 Tap 序列（每个 Tap 用自身时间查找阵型）
        else if (obj.type & 2) {
            double msPerBeat = getMsPerBeatAt(obj.time);
            double tapInterval = msPerBeat / 4.0;
            if (tapInterval < 50.0) tapInterval = 50.0;

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

            int tapCount = std::max(1, static_cast<int>(totalDurationMs / tapInterval));
            tapCount = std::min(tapCount, 32);

            for (int i = 0; i < tapCount; ++i) {
                int64_t tapTime = obj.time + static_cast<int64_t>(i * tapInterval);

                // 关键修复：用 Tap 自身的实际时间查找阵型，而非 Slider 起始时间
                const Formation* af = findFormationAt(tapTime);

                float progress = (tapCount > 1) ?
                    static_cast<float>(i) / static_cast<float>(tapCount - 1) : 0.0f;

                if (obj.slides > 1) {
                    float slideProgress = progress * obj.slides;
                    int slideIndex = static_cast<int>(slideProgress);
                    float localProgress = slideProgress - slideIndex;
                    if (slideIndex % 2 == 1) {
                        localProgress = 1.0f - localProgress;
                    }
                    progress = localProgress;
                }

                int ix, iy;
                interpolateSliderPosition(obj, progress, ix, iy);

                int32_t row, col;
                pixelToGrid(ix, iy, af->rows, af->cols, row, col);

                Note note;
                note.time = tapTime;
                note.row = row;
                note.col = col;
                note.type = NoteType::Tap;
                builder.addNote(note);
            }
        }

        // Bit 2 (4): Spinner → Hold（用自身时间查找阵型）
        else if (obj.type & 4) {
            const Formation* af = findFormationAt(obj.time);
            int32_t centerRow = af->rows / 2;
            int32_t centerCol = af->cols / 2;
            Note note;
            note.time = obj.time;
            note.row = centerRow;
            note.col = centerCol;
            note.type = NoteType::Hold;
            note.holdEnd = obj.endTime > obj.time ? obj.endTime : obj.time + 1000;
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
