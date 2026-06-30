// ──────────────────────────────────────────────────────
//  mma_parser.cpp — .mma 格式解析实现
//  行级分段累积、键值/CSV 解析、v2 Formation 六字段与 Note 类型。
// ──────────────────────────────────────────────────────

#include "beatmap/mma_parser.h"
#include "util/logger.h"
#include "util/error_codes.h"

#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace melody_matrix::beatmap {

// ── 字符串工具 ──

/// 去除首尾空白
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/// 按分隔符拆分并 trim 各段
static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, delim)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

/// 解析 int64，失败返回 false
static bool parseInt64(const std::string& s, int64_t& out) {
    try {
        out = std::stoll(s);
        return true;
    } catch (...) {
        return false;
    }
}

/// 解析 int32
static bool parseInt32(const std::string& s, int32_t& out) {
    try {
        out = static_cast<int32_t>(std::stol(s));
        return true;
    } catch (...) {
        return false;
    }
}

/// 解析 float
static bool parseFloat(const std::string& s, float& out) {
    try {
        out = std::stof(s);
        return true;
    } catch (...) {
        return false;
    }
}

/// 解析 key=value 或 key:value 行
static bool parseKeyValue(const std::string& line, std::string& key, std::string& value) {
    auto pos = line.find('=');
    if (pos == std::string::npos) {
        // 兼容冒号分隔的旧格式
        pos = line.find(':');
        if (pos == std::string::npos) return false;
    }
    key = trim(line.substr(0, pos));
    value = trim(line.substr(pos + 1));
    return !key.empty();
}

// ── 主解析流程 ──

/// 逐行扫描：首行版本号 → 段落头 → 累积行 → flush 到子解析器
util::Result<void> MmaParser::parse(const std::string& content, BeatmapBuilder& builder) {
    std::istringstream stream(content);
    std::string line;

    enum class Section { None, General, Difficulty, Meta, FormationTransformMacros, Formations, Notes };
    Section currentSection = Section::None;
    std::vector<std::string> sectionLines;
    bool firstLine = true;

    /// 将当前段落累积行交给对应子解析器
    auto flushSection = [&]() {
        if (sectionLines.empty()) return;
        util::Result<void> result(util::success());

        switch (currentSection) {
        case Section::General:
            result = parseGeneral(sectionLines, builder);
            break;
        case Section::Difficulty:
            result = parseDifficulty(sectionLines, builder);
            break;
        case Section::Meta:
            result = parseMeta(sectionLines, builder);
            break;
        case Section::FormationTransformMacros:
            // v2 宏定义段：仅解析校验，不存储（transformType 直接用 int32 保存）
            result = parseTransformMacros(sectionLines, builder);
            break;
        case Section::Formations:
            result = parseFormations(sectionLines, builder);
            break;
        case Section::Notes:
            result = parseNotes(sectionLines, builder);
            break;
        default:
            break;
        }

        if (!result.ok()) {
            // Error already logged in the sub-parser
        }
        sectionLines.clear();
    };

    while (std::getline(stream, line)) {
        line = trim(line);

        // 跳过空行与 # 注释
        if (line.empty() || line[0] == '#') continue;

        // 首行必须是 MMA1 或 MMA2
        if (firstLine) {
            firstLine = false;
            if (line == "MMA1") {
                builder.setFormatVersion("MMA1");
            } else if (line == "MMA2") {
                builder.setFormatVersion("MMA2");
            } else {
                return util::Result<void>(
                    static_cast<int32_t>(util::ErrorCode::ERROR_BEATMAP_VERSION),
                    "Expected MMA1 or MMA2 version header, got: " + line);
            }
            continue;
        }

        // 段落头 [SectionName]
        if (line.front() == '[' && line.back() == ']') {
            flushSection();
            std::string sectionName = line.substr(1, line.size() - 2);

            if (sectionName == "General")                   currentSection = Section::General;
            else if (sectionName == "Difficulty")            currentSection = Section::Difficulty;
            else if (sectionName == "Meta")                  currentSection = Section::Meta;
            else if (sectionName == "FormationTransformMacros") currentSection = Section::FormationTransformMacros;
            else if (sectionName == "Formations")            currentSection = Section::Formations;
            else if (sectionName == "Notes")                 currentSection = Section::Notes;
            else {
                MM_LOG_WARN("MmaParser", "Unknown section: [" + sectionName + "]");
                currentSection = Section::None;
            }
            continue;
        }

        // 累积当前段落数据行
        if (currentSection != Section::None) {
            sectionLines.push_back(line);
        }
    }

    // 刷出最后一个段落
    flushSection();

    MM_LOG_INFO("MmaParser", "Parse complete");
    return util::success();
}

// ── 各段落子解析器 ──

util::Result<void> MmaParser::parseGeneral(const std::vector<std::string>& lines, BeatmapBuilder& builder) {
    Meta meta;
    for (const auto& line : lines) {
        std::string key, value;
        if (!parseKeyValue(line, key, value)) continue;

        if (key == "AudioFilename") {
            meta.audioFile = value;
        } else if (key == "PreviewTime") {
            parseInt64(value, meta.previewTime);
        }
    }
    builder.setMeta(meta);
    return util::success();
}

util::Result<void> MmaParser::parseDifficulty(const std::vector<std::string>& lines, BeatmapBuilder& builder) {
    Difficulty diff;
    for (const auto& line : lines) {
        std::string key, value;
        if (!parseKeyValue(line, key, value)) continue;

        if (key == "HP")       parseFloat(value, diff.hp);
        else if (key == "OD")  parseFloat(value, diff.od);
        else if (key == "AR")  parseFloat(value, diff.ar);
    }
    builder.setDifficulty(diff);
    return util::success();
}

util::Result<void> MmaParser::parseMeta(const std::vector<std::string>& lines, BeatmapBuilder& builder) {
    // Meta 段扩展 General 中的元数据，通过 mergeMeta 合并非空字段
    Meta meta;
    for (const auto& line : lines) {
        std::string key, value;
        if (!parseKeyValue(line, key, value)) continue;

        if (key == "Title")         meta.title = value;
        else if (key == "Artist")   meta.artist = value;
        else if (key == "Creator")  meta.creator = value;
        else if (key == "Version")  meta.version = value;
    }
    builder.mergeMeta(meta);
    return util::success();
}

util::Result<void> MmaParser::parseTransformMacros(const std::vector<std::string>& lines, BeatmapBuilder& /*builder*/) {
    // v2 [FormationTransformMacros] 段：KEY=VALUE 形式定义宏名到整数。
    // 收集合法宏值集合，供 parseFormations 校验 transformType。
    m_macroValues.clear();
    for (const auto& line : lines) {
        std::string key, value;
        if (!parseKeyValue(line, key, value)) {
            MM_LOG_WARN("MmaParser", "Invalid macro line: " + line);
            continue;
        }
        int32_t v = 0;
        if (!parseInt32(value, v)) {
            MM_LOG_WARN("MmaParser", "Invalid macro value: " + line);
            continue;
        }
        m_macroValues.insert(v);
    }
    m_hasMacros = !m_macroValues.empty();
    MM_LOG_INFO("MmaParser", "Parsed " + std::to_string(m_macroValues.size()) + " transform macros");
    return util::success();
}

util::Result<void> MmaParser::parseFormations(const std::vector<std::string>& lines, BeatmapBuilder& builder) {
    for (const auto& line : lines) {
        auto parts = split(line, ',');
        if (parts.size() < 3) {
            MM_LOG_WARN("MmaParser", "Invalid formation line: " + line);
            continue;
        }

        Formation f;
        if (!parseInt64(parts[0], f.time) ||
            !parseInt32(parts[1], f.rows) ||
            !parseInt32(parts[2], f.cols)) {
            MM_LOG_WARN("MmaParser", "Invalid formation values: " + line);
            continue;
        }

        // v2 标准6字段：time,rows,cols,transformType,transformDurationMs,blockSize
        // v2 短格式默认值：transformType=NONE(0), transformDurationMs=0, blockSize=1.0
        // 兼容旧 MMA1 的 7 字段格式（第7位 noteTransformType 已废弃，忽略）
        if (parts.size() >= 4) {
            int32_t tt = 0;
            if (parseInt32(parts[3], tt)) {
                f.transformType = tt;
            }
        }
        if (parts.size() >= 5) {
            parseInt64(parts[4], f.transformDurationMs);
        }
        if (parts.size() >= 6) {
            float bs = 1.0f;
            if (parseFloat(parts[5], bs)) {
                // v2 规范第11节：blockSize > 0，推荐范围 0.5-1.5
                f.blockSize = std::clamp(bs, 0.5f, 1.5f);
            }
        }
        // parts[6] (noteTransformType) 在 v2 中已删除，旧文件存在时直接忽略

        // ── v2 规范第11节校验 ──
        // rows 范围 1-4，cols 范围 3-6
        f.rows = std::clamp(f.rows, 1, 4);
        f.cols = std::clamp(f.cols, 3, 6);
        // transformDurationMs >= 0
        if (f.transformDurationMs < 0) {
            MM_LOG_WARN("MmaParser", "Negative transformDurationMs, clamped to 0: " + line);
            f.transformDurationMs = 0;
        }
        // time 单调非递减
        if (m_lastFormationTime >= 0 && f.time < m_lastFormationTime) {
            MM_LOG_WARN("MmaParser", "Formation time not monotonic: " + line);
            f.time = m_lastFormationTime;
        }
        m_lastFormationTime = f.time;
        // transformType 必须存在于 [FormationTransformMacros] 中（若有该段）
        if (m_hasMacros && m_macroValues.find(f.transformType) == m_macroValues.end()) {
            MM_LOG_WARN("MmaParser", "transformType " + std::to_string(f.transformType) +
                        " not defined in [FormationTransformMacros]: " + line);
        }

        builder.addFormation(f);
    }
    return util::success();
}

util::Result<void> MmaParser::parseNotes(const std::vector<std::string>& lines, BeatmapBuilder& builder) {
    for (const auto& line : lines) {
        auto parts = split(line, ',');
        if (parts.size() < 3) {
            MM_LOG_WARN("MmaParser", "Invalid note line (need at least time,row,col): " + line);
            continue;
        }

        Note note;
        if (!parseInt64(parts[0], note.time) ||
            !parseInt32(parts[1], note.row) ||
            !parseInt32(parts[2], note.col)) {
            MM_LOG_WARN("MmaParser", "Invalid note values: " + line);
            continue;
        }

        // 类型：T=Tap，H=Hold；仅 3 字段时默认 Tap（向后兼容）
        if (parts.size() >= 4 && (parts[3] == "H" || parts[3] == "h")) {
            note.type = NoteType::Hold;
            if (parts.size() >= 5) {
                parseInt64(parts[4], note.holdEnd);
            } else {
                MM_LOG_WARN("MmaParser", "Hold note missing endTime: " + line);
                note.holdEnd = note.time + 500; // 缺省 Hold 时长 500ms
            }
            // v2 规范第11节：Hold note 的 endTime 必须大于 time
            if (note.holdEnd <= note.time) {
                MM_LOG_WARN("MmaParser", "Hold endTime <= time, clamped to time+1: " + line);
                note.holdEnd = note.time + 1;
            }
        } else {
            note.type = NoteType::Tap;
        }

        builder.addNote(note);
    }
    return util::success();
}

} // namespace melody_matrix::beatmap
