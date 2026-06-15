#pragma once

#include "beatmap/beatmap_parser.h"

#include <vector>
#include <cstdint>

namespace melody_matrix::beatmap {

/// osu! .osu 谱面格式的适配器解析器（仅 standard 模式 Mode=0）。
///
/// 转换策略：
///   - HitCircle → 单点 Tap
///   - Slider   → 沿曲线等间距 Tap 序列（基于 TimingPoints 拍速）
///   - Spinner  → Hold（中心位置，start→endTime）
///
/// 核心特性 — 动态呼吸矩阵：
///   不使用固定网格，而是根据 Note 的空间分布动态生成 Formation 序列。
///   当 Note 分布向屏幕边缘扩散时，矩阵膨胀（行列增大）；
///   当 Note 密度/分布收缩时，矩阵回缩，实现疏密节奏化的视觉效果。
///
/// 坐标映射：osu 游戏场 512×384 → 当前阵型网格
class OsuParser : public BeatmapParser {
public:
    util::Result<void> parse(const std::string& content, BeatmapBuilder& builder) override;
    const char* formatName() const override { return "osu"; }

private:
    // ── 内部数据结构 ──

    /// osu TimingPoint
    struct TimingPoint {
        int64_t offset = 0;     ///< 时间偏移（ms）
        double  msPerBeat = 0;  ///< 每拍毫秒数（负值=继承型，-100/value 为速度倍率）
        int     meter = 4;      ///< 节拍
        bool    uninherited = true; ///< true=独立型（定义BPM），false=继承型（定义SV）
    };

    /// 解析过程中的原始 HitObject
    struct RawHitObject {
        int     x = 0, y = 0;      ///< osu 像素坐标 (0-512, 0-384)
        int64_t time = 0;           ///< 击打时间（ms）
        int     type = 0;           ///< 类型位掩码
        // Slider 专用
        char    curveType = 'L';    ///< B/C/L/P
        std::vector<std::pair<int,int>> curvePoints; ///< 曲线控制点
        int     slides = 1;         ///< 来回次数
        double  length = 0.0;       ///< osu 像素长度
        // Spinner 专用
        int64_t endTime = 0;        ///< Spinner/Hold 结束时间
    };

    // ── 解析方法 ──

    /// 解析 TimingPoints 段落
    void parseTimingPoints(const std::vector<std::string>& lines);

    /// 解析 HitObjects 段落 → 填充 m_rawObjects
    void parseHitObjects(const std::vector<std::string>& lines);

    // ── TimingPoint 辅助 ──

    /// 获取指定时间点的活动独立型 TimingPoint（用于计算拍速）
    const TimingPoint* getBaseTimingPoint(int64_t time) const;

    /// 获取指定时间点的每拍毫秒数（考虑继承型 SV）
    double getMsPerBeatAt(int64_t time) const;

    // ── 转换方法 ──

    /// 将 osu 像素坐标量化到网格单元
    static void pixelToGrid(int x, int y, int32_t rows, int32_t cols,
                            int32_t& outRow, int32_t& outCol);

    /// 沿 Slider 曲线插值获取位置（简化版：线性插值）
    void interpolateSliderPosition(const RawHitObject& obj, float progress,
                                   int& outX, int& outY) const;

    /// 动态呼吸矩阵 — 根据 RawHitObject 的空间分布生成 Formation 序列
    std::vector<Formation> generateBreathingFormations(
        const std::vector<RawHitObject>& objects,
        int64_t windowMs = 2000,    ///< 滑动窗口大小（ms）
        int32_t minSize = 3,        ///< 最小网格维度
        int32_t maxSize = 8,        ///< 最大网格维度
        int32_t hysteresis = 1      ///< 迟滞量（避免频繁切换）
    ) const;

    // ── 成员 ──

    std::vector<TimingPoint> m_timingPoints;
    std::vector<RawHitObject> m_rawObjects;

    // Difficulty 参数
    float m_hp = 5.0f;
    float m_od = 5.0f;
    float m_ar = 5.0f;
    float m_sliderMultiplier = 1.4f;  ///< osu 默认值（不用于速度计算，保留备用）

    // General 参数
    int m_mode = 0;  ///< 游戏模式（仅支持 0=standard）
};

} // namespace melody_matrix::beatmap
