#pragma once

// ──────────────────────────────────────────────────────
//  osu_parser.h — osu! .osu 谱面适配解析器（Mode=0）
//
//  流水线：解析 TimingPoints/HitObjects → makeConvertedNotes
//  → generateFormationsAndFilter（动态阵型+滚动）
//  → arrangeRemainingNotes（行列编排）→ BeatmapBuilder。
//
//  对齐参考转换器 to_do/osz_to_mma.cpp。
// ──────────────────────────────────────────────────────

#include "beatmap/beatmap_parser.h"

#include <vector>
#include <cstdint>

namespace melody_matrix::beatmap {

/// osu! .osu 谱面格式的适配器解析器（仅 standard 模式 Mode=0）。
///
/// 转换策略（对齐 to_do/osz_to_mma.cpp 参考转换器）：
///   - HitCircle → 单点 Tap
///   - Slider   → Hold（slider 长按，起点位置，长按期间渲染边缘进度条贴图）
///   - Spinner  → Tap（转盘渲染成 tap，放在中心位置，避免触发矩阵变换/滚动）
///
/// 核心特性 — 动态呼吸矩阵：
///   根据 Note 密度（±1500ms 窗口内 note 数量）动态生成 Formation 序列。
///   通过 scheduleTransitionBefore 在 note 判定窗口前安排过渡并丢弃冲突 note。
///
/// 坐标映射：osu 游戏场 512×384 → 当前阵型网格（floor(x/512*cols)）
class OsuParser : public BeatmapParser {
public:
    util::Result<void> parse(const std::string& content, BeatmapBuilder& builder) override;
    const char* formatName() const override { return "osu"; }

private:
    // ── 常量（对齐参考转换器）──
    static constexpr int kBaseRows = 4;           ///< 初始阵型行数
    static constexpr int kBaseCols = 5;           ///< 初始阵型列数
    static constexpr int kActiveCols = 4;          ///< 活跃列数（dfjk 键数）
    static constexpr int kFormationDurationMs = 500; ///< 矩阵变换动画时长
    static constexpr int kDenseGapMs = 520;                    ///< 密集节奏判定间隔
    static constexpr int kFormationCooldownMs = 4000;           ///< 阵型变换冷却时间
    static constexpr int kFormationStabilityLookaheadMs = 2500; ///< 阵型稳定性前瞻时间
    static constexpr int kMinStableTargetNotes = 3;             ///< 稳定目标最少 note 数
    static constexpr int kHighDensityCols = 6;                  ///< 高密度目标列数
    static constexpr double kDefaultBlockSize = 1.0; ///< 非初始 formation 默认格内 item 缩放
    /// 编排节奏细分：窗宽 = msPerBeat / subdivDenominator(bpm)。
    /// ≥140 BPM → 1/3 拍；100–140 → 1/4 拍；<100 → 1/6 拍。
    static constexpr double kArrangeSubdivBpmMid  = 100.0;
    static constexpr double kArrangeSubdivBpmHigh = 140.0;
    static constexpr int kArrangeColDensityThreshold = 2; ///< 同列节奏窗内 note 数（含当前）
    static constexpr int kArrangeCellConflictWeight = 10000; ///< 同格冲突
    static constexpr int kArrangeHoldColConflictWeight = 5000; ///< Hold 占列冲突

    // ── 内部数据结构 ──

    struct TimingPoint {
        int64_t offset = 0;
        double  msPerBeat = 0;
        int     meter = 4;
        bool    uninherited = true;
    };

    struct RawHitObject {
        int     x = 0, y = 0;
        int64_t time = 0;
        int     type = 0;
        char    curveType = 'L';
        std::vector<std::pair<int,int>> curvePoints;
        int     slides = 1;
        double  length = 0.0;
        int64_t endTime = 0;
    };

    /// note 的判定/显示时间窗口（对齐参考转换器 NoteWindow）
    struct NoteWindow {
        int64_t displayStart = 0;  ///< note 开始显示时间（time - approachMs）
        int64_t earliestHit = 0;   ///< 最早可击打时间（time - missW）
        int64_t latestHit = 0;     ///< 最晚可击打时间（time + missW）
    };

    /// 转换过程中的 note（对齐参考转换器 ConvertedNote）
    struct ConvertedNote {
        int x = 0, y = 0;
        int64_t time = 0;
        int64_t endTime = 0;
        char type = 'T';
        NoteWindow window;
        NoteWindow releaseWindow;  ///< Hold 释放时间窗口
        bool dropped = false;
        int scrollWindowStart = 0; ///< 滚动模拟结束时的 activeStart（仅编排用，不参与滚动判定）
        int32_t gridRow = -1;      ///< 编排后行（>=0 有效）
        int32_t gridCol = -1;      ///< 编排后列（>=0 有效）
    };

    struct MatrixShape {
        int rows = kBaseRows;
        int cols = kBaseCols;
    };

    struct NoteRestore {
        size_t index = 0;
        ConvertedNote note;
    };

    // ── 解析方法 ──
    void parseTimingPoints(const std::vector<std::string>& lines);
    void parseHitObjects(const std::vector<std::string>& lines);

    // ── TimingPoint 辅助 ──
    const TimingPoint* getBaseTimingPoint(int64_t time) const;
    double getMsPerBeatAt(int64_t time) const;
    /// 编排用同列节奏窗（ms）= msPerBeat / arrangeRhythmSubdivDenominator(bpm)。
    double arrangeRhythmSubdivDenominator(double bpm) const;
    int64_t arrangeRhythmWindowMs(int64_t timeMs) const;
    double getSliderVelocityAt(int64_t time) const;  ///< 获取继承型 SV 倍率

    // ── 转换方法（对齐参考转换器）──

    static void pixelToGrid(int x, int y, int32_t rows, int32_t cols,
                            int32_t& outRow, int32_t& outCol);

    /// 生成 note 的时间窗口
    NoteWindow makeWindow(int64_t time) const;

    /// 将 RawHitObject 转换为 ConvertedNote（含窗口计算 + slider endTime 估算）
    std::vector<ConvertedNote> makeConvertedNotes() const;

    /// 估算 slider 结束时间（对齐参考转换器 estimateSliderEndTime）
    int64_t estimateSliderEndTime(const RawHitObject& obj) const;

    /// 根据 note 密度计算目标阵型（对齐参考转换器 targetShapeForDensity）
    MatrixShape targetShapeForDensity(const std::vector<ConvertedNote>& notes, size_t index) const;

    /// 根据行列变化计算 transformType（对齐参考转换器 transformTypeFor）
    static int transformTypeFor(const MatrixShape& from, const MatrixShape& to);

    static int previousKeptIndex(const std::vector<ConvertedNote>& notes, size_t before);
    static int nextKeptIndex(const std::vector<ConvertedNote>& notes, size_t after);
    static int64_t blockingLatestHit(const ConvertedNote& note);
    static int mappedColForShape(int x, int cols);
    static bool downgradeHoldToTapIfSafe(ConvertedNote& note, int64_t cutoffMs);
    static int blockingConflictIndexBefore(const std::vector<ConvertedNote>& notes,
                                           size_t before, int64_t cutoffMs);
    /// 遍历 noteIndex 之前所有未丢弃 note，取最大的 blockingLatestHit。
    /// 用于确保变换开始时所有前向 note（含 Hold 的释放窗口）都已判定完毕，
    /// 避免变换期间 note 仍在屏幕上导致渲染出界/无击打空间。
    static int64_t maxBlockingLatestHitBefore(const std::vector<ConvertedNote>& notes, size_t before);
    bool isDenseRhythmAround(const std::vector<ConvertedNote>& notes, size_t index) const;
    bool hasStableFormationTarget(const std::vector<ConvertedNote>& notes, size_t index,
                                   const MatrixShape& current, const MatrixShape& target) const;

    /// 在 note 窗口前安排过渡，循环丢弃冲突 note 并可恢复（对齐参考转换器 scheduleTransitionBefore）
    bool scheduleTransitionBefore(std::vector<ConvertedNote>& notes, size_t noteIndex,
                                   int64_t durationMs, bool includesFormation,
                                   int64_t lastTransitionEnd, int maxDrops, int64_t& startMs) const;

    int chooseScrollActiveStart(const std::vector<ConvertedNote>& notes, size_t noteIndex,
                                const MatrixShape& shape, int currentStart, int targetCol) const;

    /// 滚动最早可开始时刻：lastTransitionEnd、approach 触发点、当前窗内 Hold 尾部的最大值。
    int64_t scrollStartMsForNote(const std::vector<ConvertedNote>& notes, size_t noteIndex,
                                 const MatrixShape& holdShape, int holdWindowStart,
                                 int64_t lastTransitionEnd) const;

    /// 解析器侧解决滚动冲突：当前窗 Hold 阻塞 + 动画时长内够不到目标 note 则丢弃。
    bool resolveScrollConflict(std::vector<ConvertedNote>& notes, size_t noteIndex,
                               const MatrixShape& holdShape, int holdWindowStart,
                               const MatrixShape& targetShape, int nextActiveStart,
                               int64_t lastTransitionEnd, int64_t& outScrollEndMs);

    static void restoreNoteMutations(std::vector<ConvertedNote>& notes,
                                     const std::vector<NoteRestore>& restore);

    /// 生成 formations 并过滤冲突 note（对齐参考转换器 generateFormationsAndFilter）
    std::vector<Formation> generateFormationsAndFilter(std::vector<ConvertedNote>& notes);

    /// 滚动/变阵丢弃完成后编排剩余 note 的行列（不改 dropped / formations / 滚动模拟）
    void arrangeRemainingNotes(std::vector<ConvertedNote>& notes,
                                 const std::vector<Formation>& formations) const;

    /// 滚动窗口内稳定列（滚后仍留在窗内）：0-3→1-3，1-4→2-3，2-5→2-4
    static void stableArrangeColRange(int totalCols, int winStart, int winEnd,
                                      int& stableStart, int& stableEnd);

    static bool isStableArrangeCol(int col, int stableStart, int stableEnd);
    static bool isScrollEdgeCol(int col, int winStart, int winEnd,
                                int stableStart, int stableEnd);

    /// 边缘列 + 无关滚动在 latestHit 前发生且滚后出窗 → 滚动冲突，回退稳定列。
    static bool moveAffectsScroll(int newCol, int winStart, int winEnd,
                                  int stableStart, int stableEnd,
                                  int nextWinStart, int64_t nextScrollStartMs,
                                  int64_t latestHit);

    /// 查找指定时间的阵型形状
    static MatrixShape shapeAtTime(const std::vector<Formation>& formations, int64_t time);

    // ── 成员 ──
    std::vector<TimingPoint> m_timingPoints;
    std::vector<RawHitObject> m_rawObjects;

    float m_hp = 5.0f;
    float m_od = 5.0f;
    float m_ar = 5.0f;
    float m_sliderMultiplier = 1.4f;

    int m_mode = 0;
};

} // namespace melody_matrix::beatmap
