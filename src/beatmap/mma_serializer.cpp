#include "mma_serializer.h"
#include "util/error_codes.h"
#include "util/logger.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

namespace melody_matrix::beatmap {

// ──────────────────────────────────────────────────────
//  辅助函数
// ──────────────────────────────────────────────────────

/// 替换字符串中所有 Windows 不允许出现在文件名中的字符
static std::string sanitizeFilename(const std::string& name) {
    std::string result = name;
    for (auto& c : result) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    // 去除首尾空格
    while (!result.empty() && result.front() == ' ') result.erase(result.begin());
    while (!result.empty() && result.back() == ' ') result.pop_back();
    // 空名用占位
    if (result.empty()) result = "Unknown";
    return result;
}

/// 截断过长的文件名，保留最多 200 字符（为路径留余地）
static std::string truncateFilename(const std::string& name, size_t maxLen = 200) {
    if (name.size() <= maxLen) return name;
    return name.substr(0, maxLen);
}

// ──────────────────────────────────────────────────────
//  序列化
// ──────────────────────────────────────────────────────

std::string MmaSerializer::serialize(const Beatmap& beatmap, const std::string& sourceHash) {
    std::ostringstream oss;

    // ── 版本头 ──
    oss << "MMA1\n";

    // ── source_hash 注释（用于导入去重）──
    if (!sourceHash.empty()) {
        oss << "# source_hash=" << sourceHash << "\n";
    }

    oss << "\n";

    // ── [General] ──
    oss << "[General]\n";
    oss << "AudioFilename=" << beatmap.meta.audioFile << "\n";
    if (beatmap.meta.previewTime > 0) {
        oss << "PreviewTime=" << beatmap.meta.previewTime << "\n";
    }
    oss << "\n";

    // ── [Meta] ──
    oss << "[Meta]\n";
    oss << "Title=" << beatmap.meta.title << "\n";
    oss << "Artist=" << beatmap.meta.artist << "\n";
    oss << "Creator=" << beatmap.meta.creator << "\n";
    oss << "Version=" << beatmap.meta.version << "\n";
    oss << "\n";

    // ── [Difficulty] ──
    oss << "[Difficulty]\n";
    oss << std::fixed << std::setprecision(1);
    oss << "HP=" << beatmap.difficulty.hp << "\n";
    oss << "OD=" << beatmap.difficulty.od << "\n";
    oss << "AR=" << beatmap.difficulty.ar << "\n";
    oss << "\n";

    // ── [Formations] ──
    oss << "[Formations]\n";
    for (const auto& f : beatmap.formations) {
        oss << f.time << "," << f.rows << "," << f.cols;
        // 输出过渡类型和持续时间（仅非默认值时输出）
        if (f.transitionType != TransitionType::Fade || f.transitionDurationMs != 300) {
            oss << "," << static_cast<int32_t>(f.transitionType)
                << "," << f.transitionDurationMs;
        }
        oss << "\n";
    }
    oss << "\n";

    // ── [Notes] ──
    oss << "[Notes]\n";
    for (const auto& n : beatmap.notes) {
        oss << n.time << "," << n.row << "," << n.col << ",";
        if (n.type == NoteType::Hold) {
            oss << "H," << n.holdEnd;
        } else {
            oss << "T";
        }
        oss << "\n";
    }

    return oss.str();
}

util::Result<void> MmaSerializer::writeToFile(const Beatmap& beatmap,
                                               const std::string& filePath,
                                               const std::string& sourceHash) {
    try {
        std::string content = serialize(beatmap, sourceHash);

        // 确保父目录存在
        std::filesystem::path p(filePath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }

        std::ofstream ofs(filePath, std::ios::binary);
        if (!ofs) {
            return util::Result<void>(
                static_cast<int32_t>(util::ErrorCode::ERROR_IO),
                "Failed to open file for writing: " + filePath);
        }
        ofs << content;
        if (!ofs) {
            return util::Result<void>(
                static_cast<int32_t>(util::ErrorCode::ERROR_IO),
                "Failed to write beatmap file: " + filePath);
        }

        MM_LOG_INFO("MmaSerializer", "Beatmap written to: %s", filePath.c_str());
        return util::success();
    } catch (const std::exception& e) {
        return util::Result<void>(
            static_cast<int32_t>(util::ErrorCode::ERROR_IO),
            std::string("Exception writing beatmap: ") + e.what());
    }
}

std::string MmaSerializer::readSourceHash(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs) return "";

    std::string line;
    // 只需读前几行找 source_hash 注释
    int lineCount = 0;
    while (std::getline(ifs, line) && lineCount < 10) {
        ++lineCount;
        // 去除 BOM 和空白
        if (!line.empty() && line[0] == '\xEF' && line.size() >= 3) {
            line = line.substr(3);
        }
        // 查找 # source_hash=xxxx
        if (line.rfind("# source_hash=", 0) == 0) {
            return line.substr(15); // 15 = strlen("# source_hash=")
        }
        // 如果已经过了注释区域（遇到非注释、非空行），可以停止
        if (!line.empty() && line[0] != '#' && line[0] != '\r' && line[0] != '\n') {
            // 继续读，因为可能 source_hash 在空行后面
            // 但为效率起见，最多读 10 行
        }
    }
    return "";
}

} // namespace melody_matrix::beatmap
