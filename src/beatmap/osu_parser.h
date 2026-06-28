#pragma once

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
    static constexpr int kMinRows = 1;
    static constexpr int kMaxRows = 4;
    static constexpr int kMinCols = 3;
    static constexpr int kMaxCols = 6;
    static constexpr int kActiveCols = 4;          ///< 活跃列数（dfjk 键数）
    static constexpr int kFormationDurationMs = 500; ///< 矩阵变换动画时长
    static constexpr int kScrollDurationMs = 200;   ///< 滚动动画时长
    static constexpr double kDefaultBlockSize = 0.9; ///< 非初始 formation 默认 blockSize

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
        bool dropped = false;
    };

    struct MatrixShape {
        int rows = kBaseRows;
        int cols = kBaseCols;
    };

    // ── 解析方法 ──
    void parseTimingPoints(const std::vector<std::string>& lines);
    void parseHitObjects(const std::vector<std::string>& lines);

    // ── TimingPoint 辅助 ──
    const TimingPoint* getBaseTimingPoint(int64_t time) const;
    double getMsPerBeatAt(int64_t time) const;
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

    /// 在 note 窗口前安排过渡，丢弃冲突 note（对齐参考转换器 scheduleTransitionBefore）
    bool scheduleTransitionBefore(std::vector<ConvertedNote>& notes, size_t noteIndex,
                                   int64_t durationMs, bool includesFormation,
                                   int64_t lastTransitionEnd, int64_t& startMs) const;

    /// 生成 formations 并过滤冲突 note（对齐参考转换器 generateFormationsAndFilter）
    std::vector<Formation> generateFormationsAndFilter(std::vector<ConvertedNote>& notes);

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
