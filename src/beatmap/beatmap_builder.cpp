// ──────────────────────────────────────────────────────
//  beatmap_builder.cpp — 谱面构建与验证实现
//  八条规则：版本、音频、边界、阵型、时序、段落、Hold 重叠、变阵缓冲。
// ──────────────────────────────────────────────────────

#include "beatmap/beatmap_builder.h"
#include "util/logger.h"

#include <algorithm>

namespace melody_matrix::beatmap {

// ── 流式 setter ──

BeatmapBuilder& BeatmapBuilder::setMeta(const Meta& meta) {
    m_meta = meta;  // 整体覆盖元数据
    return *this;   // 链式调用
}

BeatmapBuilder& BeatmapBuilder::mergeMeta(const Meta& meta) {
    // 仅合并非空字段，保留 builder 中已有值
    if (!meta.title.empty())     m_meta.title = meta.title;
    if (!meta.artist.empty())    m_meta.artist = meta.artist;
    if (!meta.creator.empty())   m_meta.creator = meta.creator;
    if (!meta.version.empty())   m_meta.version = meta.version;
    if (!meta.audioFile.empty()) m_meta.audioFile = meta.audioFile;
    if (meta.previewTime != 0)   m_meta.previewTime = meta.previewTime;
    return *this;
}

BeatmapBuilder& BeatmapBuilder::setDifficulty(const Difficulty& diff) {
    m_difficulty = diff;  // 设置 HP/OD/AR 等难度参数
    return *this;
}

BeatmapBuilder& BeatmapBuilder::setFormatVersion(const std::string& version) {
    m_formatVersion = version;  // 记录来源格式（MMA1/MMA2/osu）
    return *this;
}

// ── 流式 adder ──

BeatmapBuilder& BeatmapBuilder::addFormation(const Formation& formation) {
    m_formations.push_back(formation);  // 追加单条阵型
    return *this;
}

BeatmapBuilder& BeatmapBuilder::addNote(const Note& note) {
    m_notes.push_back(note);  // 追加单条音符
    return *this;
}

BeatmapBuilder& BeatmapBuilder::addFormations(const std::vector<Formation>& formations) {
    m_formations.insert(m_formations.end(), formations.begin(), formations.end());  // 批量追加阵型
    return *this;
}

BeatmapBuilder& BeatmapBuilder::addNotes(const std::vector<Note>& notes) {
    m_notes.insert(m_notes.end(), notes.begin(), notes.end());  // 批量追加音符
    return *this;
}

// ── 验证辅助函数 ──

/// 规则 6：Meta 与 Difficulty 必需字段存在
bool BeatmapBuilder::validateSections() const {
    // 规则 6a：Meta 段至少要有 title 或 version 之一
    if (m_meta.title.empty() && m_meta.version.empty()) {
        MM_LOG_ERROR("BeatmapBuilder", "Missing required section: Meta");
        return false;
    }
    // 规则 6b：General 段 audioFile 非空
    if (m_meta.audioFile.empty()) {
        MM_LOG_ERROR("BeatmapBuilder", "Missing required section: General (audioFile)");
        return false;
    }
    // 规则 6c：Difficulty 段 HP/OD/AR 不得为负（未设置时为默认正值）
    if (m_difficulty.hp < 0 || m_difficulty.od < 0 || m_difficulty.ar < 0) {
        MM_LOG_ERROR("BeatmapBuilder", "Missing required section: Difficulty");
        return false;
    }
    return true;
}

/// 规则 4：每个阵型 rows≥1、cols≥1，且至少一条 Formation
bool BeatmapBuilder::validateFormations() const {
    // 规则 4a：逐条检查阵型尺寸
    for (const auto& f : m_formations) {
        if (f.rows < 1 || f.cols < 1) {
            MM_LOG_ERROR("BeatmapBuilder", "Formation at t=" +
                         std::to_string(f.time) + "ms has invalid dimensions: " +
                         std::to_string(f.rows) + "x" + std::to_string(f.cols));
            return false;
        }
    }
    // 规则 4b：至少定义一条阵型（t=0 初始阵型）
    if (m_formations.empty()) {
        MM_LOG_ERROR("BeatmapBuilder", "No formations defined");
        return false;
    }
    return true;
}

/// 规则 5：Formation 严格升序，Note 非降序
bool BeatmapBuilder::validateTimeOrder() const {
    // 规则 5a：Formation.time 必须严格递增（不允许同刻多条）
    for (size_t i = 1; i < m_formations.size(); ++i) {
        if (m_formations[i].time <= m_formations[i - 1].time) {
            MM_LOG_ERROR("BeatmapBuilder", "Formation time not ascending: [" +
                         std::to_string(i - 1) + "]=" + std::to_string(m_formations[i - 1].time) +
                         " >= [" + std::to_string(i) + "]=" + std::to_string(m_formations[i].time));
            return false;
        }
    }
    // 规则 5b：Note.time 非降序（同刻多 note 允许）
    for (size_t i = 1; i < m_notes.size(); ++i) {
        if (m_notes[i].time < m_notes[i - 1].time) {
            MM_LOG_ERROR("BeatmapBuilder", "Note time not ascending: [" +
                         std::to_string(i - 1) + "]=" + std::to_string(m_notes[i - 1].time) +
                         " > [" + std::to_string(i) + "]=" + std::to_string(m_notes[i].time));
            return false;
        }
    }
    return true;
}

/// 规则 3：音符 (row,col) 落在其时刻活动阵型边界内
bool BeatmapBuilder::validateNoteBounds() const {
    // 规则 3：每个 note 在其 time 对应的活动阵型 [0,rows)×[0,cols) 内
    for (const auto& note : m_notes) {
        const Formation* formation = nullptr;
        // 找 time 之前（含）最后一条 Formation
        for (const auto& f : m_formations) {
            if (f.time <= note.time) {
                formation = &f;
            } else {
                break;  // formations 升序，后续更晚
            }
        }
        // 没有任何阵型覆盖该时刻
        if (!formation) {
            MM_LOG_ERROR("BeatmapBuilder", "Note at t=" + std::to_string(note.time) +
                         "ms has no active formation");
            return false;
        }
        // row/col 越界检查
        if (note.row < 0 || note.row >= formation->rows ||
            note.col < 0 || note.col >= formation->cols) {
            MM_LOG_ERROR("BeatmapBuilder", "Note at t=" + std::to_string(note.time) +
                         "ms (" + std::to_string(note.row) + "," + std::to_string(note.col) +
                         ") out of bounds for formation " +
                         std::to_string(formation->rows) + "x" + std::to_string(formation->cols));
            return false;
        }
    }
    return true;
}

/// 规则 2：音频路径非空（运行时存在性另检）
bool BeatmapBuilder::validateAudioFile() const {
    // 规则 2：audioFile 字符串不得为空
    if (m_meta.audioFile.empty()) {
        MM_LOG_ERROR("BeatmapBuilder", "Audio file path is empty");
        return false;
    }
    return true;
}

/// 规则 1：formatVersion 由解析器设置且非空
bool BeatmapBuilder::validateVersion() const {
    // 规则 1：formatVersion 非空（"MMA1"/"MMA2"/"osu" 等）
    if (m_formatVersion.empty()) {
        MM_LOG_ERROR("BeatmapBuilder", "Format version is empty");
        return false;
    }
    return true;
}

bool BeatmapBuilder::validateHoldTapOverlap() {
    // 规则 7（软验证）：同列 Hold 与 Tap/其他 Hold 时间区间禁止重叠
    // 策略：丢弃与 Hold 重叠的非 Hold note，保留 Hold 本身；始终返回 true
    std::vector<size_t> toRemove;  // 未直接使用，标记通过 marked 数组
    std::vector<bool> marked(m_notes.size(), false);  // 待删除标记

    // 外层：遍历每条 Hold
    for (size_t i = 0; i < m_notes.size(); ++i) {
        if (m_notes[i].type != NoteType::Hold) continue;

        int64_t holdStart = m_notes[i].time;    // Hold 起始
        int64_t holdEnd   = m_notes[i].holdEnd; // Hold 结束（不含端点比较用开区间）
        int32_t col       = m_notes[i].col;     // Hold 所在列

        // 内层：检查同列其他 note 是否落在 (holdStart, holdEnd) 开区间内
        for (size_t j = 0; j < m_notes.size(); ++j) {
            if (i == j || marked[j]) continue;       // 跳过自身与已标记
            if (m_notes[j].col != col) continue;     // 不同列不冲突

            int64_t noteTime = m_notes[j].time;
            // 重叠判定：note 时间在 Hold 开区间内
            if (noteTime > holdStart && noteTime < holdEnd) {
                MM_LOG_WARN("BeatmapBuilder", "Hold+Tap overlap: discarding note at t=" +
                             std::to_string(noteTime) + "ms col=" + std::to_string(col) +
                             " (overlaps Hold at t=" + std::to_string(holdStart) + "ms)");
                marked[j] = true;  // 标记丢弃
            }
        }
    }

    // 按 marked 移除 note（remove_if 配合索引计数）
    size_t removed = 0;
    m_notes.erase(std::remove_if(m_notes.begin(), m_notes.end(),
        [&](const Note&) { bool r = marked[removed]; ++removed; return r; }),
        m_notes.end());

    return true; // 软验证：冲突已丢弃，不阻断 build
}

bool BeatmapBuilder::validateFormationBuffer() {
    // 规则 8（软验证）：每条 Formation（除首条）前 500ms 内不得有 note
    // 策略：丢弃 buffer 内 note；始终返回 true
    static constexpr int64_t BUFFER_MS = 500;  // 变阵缓冲时长

    std::vector<bool> marked(m_notes.size(), false);

    // 从第二条 Formation 起检查（fi=1）
    for (size_t fi = 1; fi < m_formations.size(); ++fi) {
        int64_t formationTime = m_formations[fi].time;       // 变阵生效时刻
        int64_t bufferStart   = formationTime - BUFFER_MS;   // 缓冲区间起点 [bufferStart, formationTime)

        for (size_t ni = 0; ni < m_notes.size(); ++ni) {
            if (marked[ni]) continue;
            // note 落在变阵前 500ms 缓冲区内 → 丢弃
            if (m_notes[ni].time >= bufferStart && m_notes[ni].time < formationTime) {
                MM_LOG_WARN("BeatmapBuilder", "Formation buffer: discarding note at t=" +
                             std::to_string(m_notes[ni].time) + "ms" +
                             " (within 500ms buffer of formation at t=" +
                             std::to_string(formationTime) + "ms)");
                marked[ni] = true;
            }
        }
    }

    // 移除被标记的音符
    size_t removed = 0;
    m_notes.erase(std::remove_if(m_notes.begin(), m_notes.end(),
        [&](const Note&) { bool r = marked[removed]; ++removed; return r; }),
        m_notes.end());

    return true; // 软验证：冲突已丢弃，不阻断 build
}

// ── 构建入口 ──

/// 排序后依次硬验证，再软验证丢弃冲突音符，最后组装 Beatmap
util::Result<Beatmap> BeatmapBuilder::build() {
    // 构建前按时间排序（解析器可能乱序写入）
    std::sort(m_formations.begin(), m_formations.end(),
              [](const Formation& a, const Formation& b) { return a.time < b.time; });
    std::sort(m_notes.begin(), m_notes.end(),
              [](const Note& a, const Note& b) { return a.time < b.time; });

    // ── 硬验证：任一失败则整谱 build 失败 ──
    if (!validateVersion()) {
        return util::Result<Beatmap>(static_cast<int32_t>(util::ErrorCode::ERROR_BEATMAP_VERSION),
                                     "Beatmap version string is empty");
    }
    if (!validateAudioFile()) {
        return util::Result<Beatmap>(static_cast<int32_t>(util::ErrorCode::ERROR_BEATMAP_AUDIO_MISSING),
                                     "Audio file is missing or not specified");
    }
    if (!validateNoteBounds()) {
        return util::Result<Beatmap>(static_cast<int32_t>(util::ErrorCode::ERROR_BEATMAP_NOTE_OOB),
                                     "Note coordinates out of formation bounds");
    }
    if (!validateFormations()) {
        return util::Result<Beatmap>(static_cast<int32_t>(util::ErrorCode::ERROR_BEATMAP_FORMATION),
                                     "Formation dimensions invalid (rows>=1, cols>=1 required)");
    }
    if (!validateTimeOrder()) {
        return util::Result<Beatmap>(static_cast<int32_t>(util::ErrorCode::ERROR_BEATMAP_TIME_ORDER),
                                     "Formation/Note times must be in ascending order");
    }
    if (!validateSections()) {
        return util::Result<Beatmap>(static_cast<int32_t>(util::ErrorCode::ERROR_BEATMAP_MISSING_SECTION),
                                     "Required beatmap section is missing");
    }

    // 软验证：丢弃冲突音符而非拒绝整个谱面
    validateHoldTapOverlap();
    validateFormationBuffer();

    // ── 组装 Beatmap 并返回 ──
    Beatmap bm;
    bm.meta       = std::move(m_meta);
    bm.difficulty = m_difficulty;
    bm.formations = std::move(m_formations);
    bm.notes      = std::move(m_notes);

    MM_LOG_INFO("BeatmapBuilder", "Built beatmap: " + bm.meta.title +
                " [" + bm.meta.version + "] — " +
                std::to_string(bm.noteCount()) + " notes, " +
                std::to_string(bm.formations.size()) + " formations");

    return util::Result<Beatmap>(std::move(bm));
}

void BeatmapBuilder::reset() {
    m_meta = Meta{};              // 清空元数据
    m_difficulty = Difficulty{};  // 重置难度
    m_formations.clear();         // 清空阵型
    m_notes.clear();              // 清空音符
    m_formatVersion.clear();      // 清空格式版本
}

} // namespace melody_matrix::beatmap
