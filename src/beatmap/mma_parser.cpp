#include "beatmap/mma_parser.h"
#include "util/logger.h"
#include "util/error_codes.h"

#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace melody_matrix::beatmap {

// ── Helper: trim whitespace ──
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ── Helper: split string by delimiter ──
static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, delim)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

// ── Helper: parse int64 ──
static bool parseInt64(const std::string& s, int64_t& out) {
    try {
        out = std::stoll(s);
        return true;
    } catch (...) {
        return false;
    }
}

// ── Helper: parse int32 ──
static bool parseInt32(const std::string& s, int32_t& out) {
    try {
        out = static_cast<int32_t>(std::stol(s));
        return true;
    } catch (...) {
        return false;
    }
}

// ── Helper: parse float ──
static bool parseFloat(const std::string& s, float& out) {
    try {
        out = std::stof(s);
        return true;
    } catch (...) {
        return false;
    }
}

// ── Helper: parse key=value or key:value line ──
static bool parseKeyValue(const std::string& line, std::string& key, std::string& value) {
    auto pos = line.find('=');
    if (pos == std::string::npos) {
        // Try colon separator for backward compatibility
        pos = line.find(':');
        if (pos == std::string::npos) return false;
    }
    key = trim(line.substr(0, pos));
    value = trim(line.substr(pos + 1));
    return !key.empty();
}

// ── Main parse ──

util::Result<void> MmaParser::parse(const std::string& content, BeatmapBuilder& builder) {
    std::istringstream stream(content);
    std::string line;

    enum class Section { None, General, Difficulty, Meta, Formations, Notes };
    Section currentSection = Section::None;
    std::vector<std::string> sectionLines;
    bool firstLine = true;

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

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // First line must be the format version
        if (firstLine) {
            firstLine = false;
            if (line != "MMA1") {
                return util::Result<void>(
                    static_cast<int32_t>(util::ErrorCode::ERROR_BEATMAP_VERSION),
                    "Expected MMA1 version header, got: " + line);
            }
            builder.setFormatVersion("MMA1");
            continue;
        }

        // Check for section header
        if (line.front() == '[' && line.back() == ']') {
            flushSection();
            std::string sectionName = line.substr(1, line.size() - 2);

            if (sectionName == "General")          currentSection = Section::General;
            else if (sectionName == "Difficulty")   currentSection = Section::Difficulty;
            else if (sectionName == "Meta")          currentSection = Section::Meta;
            else if (sectionName == "Formations")    currentSection = Section::Formations;
            else if (sectionName == "Notes")         currentSection = Section::Notes;
            else {
                MM_LOG_WARN("MmaParser", "Unknown section: [" + sectionName + "]");
                currentSection = Section::None;
            }
            continue;
        }

        // Accumulate lines for current section
        if (currentSection != Section::None) {
            sectionLines.push_back(line);
        }
    }

    // Flush last section
    flushSection();

    MM_LOG_INFO("MmaParser", "Parse complete");
    return util::success();
}

// ── Section parsers ──

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
    // Meta section extends the General meta
    // We need to read the current meta, update it, and set it back
    // For now, we'll create a new Meta and merge
    Meta meta;
    // The builder already has meta from General, but we need to read it
    // Since our builder doesn't expose the meta, we update via a temporary
    // This is a design limitation — we'll add title/artist/creator/version
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

        // 可选字段：transitionType (parts[3]) 和 transitionDurationMs (parts[4])
        if (parts.size() >= 4) {
            int32_t tt = 0;
            if (parseInt32(parts[3], tt)) {
                tt = std::clamp(tt, 0, 2);
                f.transitionType = static_cast<TransitionType>(tt);
            }
        }
        if (parts.size() >= 5) {
            parseInt64(parts[4], f.transitionDurationMs);
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

        // Parse type: T=tap, H=hold; default to Tap if only 3 fields (backward compat)
        if (parts.size() >= 4 && (parts[3] == "H" || parts[3] == "h")) {
            note.type = NoteType::Hold;
            if (parts.size() >= 5) {
                parseInt64(parts[4], note.holdEnd);
            } else {
                MM_LOG_WARN("MmaParser", "Hold note missing endTime: " + line);
                note.holdEnd = note.time + 500; // Default 500ms hold
            }
        } else {
            note.type = NoteType::Tap;
        }

        builder.addNote(note);
    }
    return util::success();
}

} // namespace melody_matrix::beatmap
