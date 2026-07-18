// ──────────────────────────────────────────────────────
//  mma_serializer.cpp — .mma 序列化实现
//  输出 MMA2 各标准段落；短格式省略默认 Formation 字段。
// ──────────────────────────────────────────────────────

#include "mma_serializer.h"
#include "util/error_codes.h"
#include "util/logger.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

namespace melody_matrix::beatmap {

// ── 序列化 ──

/// 将 Beatmap 序列化为 MMA2 文本（含宏段、短格式 Formation 优化）
std::string MmaSerializer::serialize(const Beatmap& beatmap, const std::string& sourceHash) {
    std::ostringstream oss;

    // ── 版本头（v2）──
    oss << "MMA2\n";

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
    oss << std::fixed << std::setprecision(1);  // 一位小数
    oss << "HP=" << beatmap.difficulty.hp << "\n";
    oss << "OD=" << beatmap.difficulty.od << "\n";
    oss << "AR=" << beatmap.difficulty.ar << "\n";
    oss << "\n";

    // ── [FormationTransformMacros]（v2 新增）──
    oss << "[FormationTransformMacros]\n";
    oss << "MATRIX_TRANSFORM_NONE=0\n";
    oss << "MATRIX_TRANSFORM_SCALE_ONLY=100\n";
    oss << "MATRIX_TRANSFORM_SLIDE_ROW_ADD_TOP=201\n";
    oss << "MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTTOM=202\n";
    oss << "MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTH=203\n";
    oss << "MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_TOP=204\n";
    oss << "MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTTOM=205\n";
    oss << "MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTH=206\n";
    oss << "MATRIX_TRANSFORM_SLIDE_COL_ADD_LEFT=221\n";
    oss << "MATRIX_TRANSFORM_SLIDE_COL_ADD_RIGHT=222\n";
    oss << "MATRIX_TRANSFORM_SLIDE_COL_ADD_BOTH=223\n";
    oss << "MATRIX_TRANSFORM_SLIDE_COL_REMOVE_LEFT=224\n";
    oss << "MATRIX_TRANSFORM_SLIDE_COL_REMOVE_RIGHT=225\n";
    oss << "MATRIX_TRANSFORM_SLIDE_COL_REMOVE_BOTH=226\n";
    oss << "MATRIX_TRANSFORM_SLIDE_SINGLE_AXIS_COMPLEX=299\n";
    oss << "MATRIX_TRANSFORM_ROTATE_ROWS_COLS_ADD=301\n";
    oss << "MATRIX_TRANSFORM_ROTATE_ROWS_ADD_COLS_REMOVE=302\n";
    oss << "MATRIX_TRANSFORM_ROTATE_ROWS_REMOVE_COLS_ADD=303\n";
    oss << "MATRIX_TRANSFORM_ROTATE_ROWS_COLS_REMOVE=304\n";
    oss << "MATRIX_TRANSFORM_ROTATE_COMPLEX=399\n";
    oss << "\n";

    // ── [Formations]（v2 标准6字段）──
    oss << "[Formations]\n";
    for (const auto& f : beatmap.formations) {
        oss << f.time << "," << f.rows << "," << f.cols;
        // v2 短格式：transformType=0 && transformDurationMs=0 && blockSize=1.0 时只输出前3字段
        if (f.transformType != MatrixTransform::NONE ||
            f.transformDurationMs != 0 ||
            f.blockSize != 1.0f) {
            oss << "," << f.transformType
                << "," << f.transformDurationMs
                << "," << f.blockSize;
        }
        oss << "\n";
    }
    oss << "\n";

    // ── [Notes] ──
    oss << "[Notes]\n";
    for (const auto& n : beatmap.notes) {
        oss << n.time << "," << n.row << "," << n.col << ",";
        if (n.type == NoteType::Hold) {
            oss << "H," << n.holdEnd;  // Hold 带结束时间
        } else {
            oss << "T";                // Tap 无额外字段
        }
        oss << "\n";
    }

    return oss.str();
}

/// 序列化并写入磁盘，自动创建父目录
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

/// 读取文件头最多 10 行，解析 # source_hash= 注释
std::string MmaSerializer::readSourceHash(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs) return "";  // 文件打不开

    std::string line;
    int lineCount = 0;
    while (std::getline(ifs, line) && lineCount < 10) {
        ++lineCount;
        // 去除 UTF-8 BOM
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line = line.substr(3);
        }
        // 匹配 # source_hash=xxxx
        if (line.rfind("# source_hash=", 0) == 0) {
            std::string hash = line.substr(14);  // strlen("# source_hash=") == 14
            // Windows getline 可能残留 \r；去空白避免与 sha256 字符串对不上 → erase 失败
            while (!hash.empty() && (hash.back() == '\r' || hash.back() == '\n' ||
                                     hash.back() == ' ' || hash.back() == '\t')) {
                hash.pop_back();
            }
            while (!hash.empty() && (hash.front() == ' ' || hash.front() == '\t')) {
                hash.erase(hash.begin());
            }
            return hash;
        }
    }
    return "";  // 未找到 source_hash
}

} // namespace melody_matrix::beatmap
