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
    m_meta = meta;
    return *this;
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
    m_difficulty = diff;
    return *this;
}

BeatmapBuilder& BeatmapBuilder::setFormatVersion(const std::string& version) {
    m_formatVersion = version;
    return *this;
}

// ── 流式 adder ──

BeatmapBuilder& BeatmapBuilder::addFormation(const Formation& formation) {
    m_formations.push_back(formation);
    return *this;
}

BeatmapBuilder& BeatmapBuilder::addNote(const Note& note) {
    m_notes.push_back(note);
    return *this;
}

BeatmapBuilder& BeatmapBuilder::addFormations(const std::vector<Formation>& formations) {
    m_formations.insert(m_formations.end(), formations.begin(), formations.end());
    return *this;
}

BeatmapBuilder& BeatmapBuilder::addNotes(const std::vector<Note>& notes) {
    m_notes.insert(m_notes.end(), notes.begin(), notes.end());
    return *this;
}

// ── 验证辅助函数 ──

/// 规则 6：Meta 与 Difficulty 必需字段存在
bool BeatmapBuilder::validateSections() const {
    // 规则 6：Meta 至少含 title 或 version；General 含 audioFile；Difficulty 有效
    if (m_meta.title.empty() && m_meta.version.empty()) {
        MM_LOG_ERROR("BeatmapBuilder", "Missing required section: Meta");
        return false;
    }
    if (m_meta.audioFile.empty()) {
        MM_LOG_ERROR("BeatmapBuilder", "Missing required section: General (audioFile)");
        return false;
    }
    if (m_difficulty.hp < 0 || m_difficulty.od < 0 || m_difficulty.ar < 0) {
        MM_LOG_ERROR("BeatmapBuilder", "Missing required section: Difficulty");
        return false;
    }
    return true;
}

/// 规则 4：每个阵型 rows≥1、cols≥1，且至少一条 Formation
bool BeatmapBuilder::validateFormations() const {
    // 规则 4：每个阵型 rows≥1 且 cols≥1
    for (const auto& f : m_formations) {
        if (f.rows < 1 || f.cols < 1) {
            MM_LOG_ERROR("BeatmapBuilder", "Formation at t=" +
                         std::to_string(f.time) + "ms has invalid dimensions: " +
                         std::to_string(f.rows) + "x" + std::to_string(f.cols));
            return false;
        }
    }
    if (m_formations.empty()) {
        MM_LOG_ERROR("BeatmapBuilder", "No formations defined");
        return false;
    }
    return true;
}

/// 规则 5：Formation 严格升序，Note 非降序
bool BeatmapBuilder::validateTimeOrder() const {
    // 规则 5：阵型与音符时间单调
    for (size_t i = 1; i < m_formations.size(); ++i) {
        if (m_formations[i].time <= m_formations[i - 1].time) {
            MM_LOG_ERROR("BeatmapBuilder", "Formation time not ascending: [" +
                         std::to_string(i - 1) + "]=" + std::to_string(m_formations[i - 1].time) +
                         " >= [" + std::to_string(i) + "]=" + std::to_string(m_formations[i].time));
            return false;
        }
    }
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
    // 规则 3：每个音符坐标在对应时刻阵型范围内
    for (const auto& note : m_notes) {
        const Formation* formation = nullptr;
        for (const auto& f : m_formations) {
            if (f.time <= note.time) {
                formation = &f;
            } else {
                break;
            }
        }
        if (!formation) {
            MM_LOG_ERROR("BeatmapBuilder", "Note at t=" + std::to_string(note.time) +
                         "ms has no active formation");
            return false;
        }
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
    // 规则 2：音频文件路径非空
    if (m_meta.audioFile.empty()) {
        MM_LOG_ERROR("BeatmapBuilder", "Audio file path is empty");
        return false;
    }
    return true;
}

/// 规则 1：formatVersion 由解析器设置且非空
bool BeatmapBuilder::validateVersion() const {
    // 规则 1：版本字符串非空（"MMA1"/"MMA2"/"osu" 等）
    if (m_formatVersion.empty()) {
        MM_LOG_ERROR("BeatmapBuilder", "Format version is empty");
        return false;
    }
    return true;
}

bool BeatmapBuilder::validateHoldTapOverlap() {
    // Rule 7: 同列 Hold+Tap 禁止重叠（软验证）
    // 丢弃与 Hold 重叠的 Tap/其他 Hold，保留 Hold 本身
    std::vector<size_t> toRemove;
    std::vector<bool> marked(m_notes.size(), false);

    for (size_t i = 0; i < m_notes.size(); ++i) {
        if (m_notes[i].type != NoteType::Hold) continue;

        int64_t holdStart = m_notes[i].time;
        int64_t holdEnd   = m_notes[i].holdEnd;
        int32_t col       = m_notes[i].col;

        for (size_t j = 0; j < m_notes.size(); ++j) {
            if (i == j || marked[j]) continue;
            if (m_notes[j].col != col) continue;

            int64_t noteTime = m_notes[j].time;
            if (noteTime > holdStart && noteTime < holdEnd) {
                MM_LOG_WARN("BeatmapBuilder", "Hold+Tap overlap: discarding note at t=" +
                             std::to_string(noteTime) + "ms col=" + std::to_string(col) +
                             " (overlaps Hold at t=" + std::to_string(holdStart) + "ms)");
                marked[j] = true;
            }
        }
    }

    // 移除被标记的音符
    size_t removed = 0;
    m_notes.erase(std::remove_if(m_notes.begin(), m_notes.end(),
        [&](const Note&) { bool r = marked[removed]; ++removed; return r; }),
        m_notes.end());

    return true; // 始终通过，冲突音符已被丢弃
}

bool BeatmapBuilder::validateFormationBuffer() {
    // Rule 8: 阵型变化前 500ms 内的音符将被丢弃（软验证）
    static constexpr int64_t BUFFER_MS = 500;

    std::vector<bool> marked(m_notes.size(), false);

    for (size_t fi = 1; fi < m_formations.size(); ++fi) {
        int64_t formationTime = m_formations[fi].time;
        int64_t bufferStart   = formationTime - BUFFER_MS;

        for (size_t ni = 0; ni < m_notes.size(); ++ni) {
            if (marked[ni]) continue;
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

    return true; // 始终通过，冲突音符已被丢弃
}

// ── 构建入口 ──

/// 排序后依次硬验证，再软验证丢弃冲突音符，最后组装 Beatmap
util::Result<Beatmap> BeatmapBuilder::build() {
    // 构建前按时间排序
    std::sort(m_formations.begin(), m_formations.end(),
              [](const Formation& a, const Formation& b) { return a.time < b.time; });
    std::sort(m_notes.begin(), m_notes.end(),
              [](const Note& a, const Note& b) { return a.time < b.time; });

    // ── 执行全部硬验证规则 ──
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
    m_meta = Meta{};
    m_difficulty = Difficulty{};
    m_formations.clear();
    m_notes.clear();
    m_formatVersion.clear();
}

} // namespace melody_matrix::beatmap
