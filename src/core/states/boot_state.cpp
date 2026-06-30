// ============================================================
// boot_state.cpp — 启动状态实现
//
// 职责：
//   - 播放 "Mi hits" 品牌加载动画
//   - 后台线程扫描铺面（无 GL 上下文）
//   - 主线程预加载分组背景纹理
//   - 动画 + 加载均完成后过渡到 MainMenu
// ============================================================
#include "boot_state.h"
#include "core/kernel.h"
#include "core/states/song_select_state.h"
#include "renderer/texture_cache.h"
#include "ui/theme.h"
#include "util/logger.h"

#include "imgui.h"
#include <cmath>
#include <algorithm>

namespace melody_matrix::core {

// ══════════════════════════════════════════════════════════════════════════════
//  生命周期
// ══════════════════════════════════════════════════════════════════════════════

/// 进入启动状态，重置所有动画与加载标志
void BootState::onEnter() {
    MM_LOG_INFO("Boot", "Entering Boot state");
    m_time = 0.0f;
    m_animationDone = false;
    m_transitionRequested = false;
    m_loaderStarted = false;
    m_preloadRequested = false;
    m_texturesLoaded = false;
    m_preloadPaths.clear();
}

/// 退出启动状态，阻塞等待后台加载线程完成
void BootState::onExit() {
    MM_LOG_INFO("Boot", "Exiting Boot state");
    // 确保加载线程完成
    m_loader.wait();
}

/// 每帧更新：启动异步扫描、主线程纹理预加载、检测过渡条件
GameState BootState::update(float dt) {
    m_time += dt;                                            // 累计动画时间

    // 后台线程：仅 scanBeatmaps（文件 I/O + 解析），不可调用 OpenGL
    if (!m_loaderStarted) {
        m_loaderStarted = true;
        m_loader.setTask([](AssetLoader& loader) {
            auto* songSelect = Kernel::instance().stateManager().getStateAs<SongSelectState>(GameState::SongSelect);
            if (songSelect) {
                loader.setProgress(0.1f);
                songSelect->scanBeatmaps();                  // 扫描 assets/beatmaps 下 .mma/.osu
                loader.setProgress(0.8f);
            }
        });
        m_loader.start();                                    // 启动后台线程
    }

    // 扫描完成后异步请求预加载；Kernel 每帧 processPendingUploads 在主线程上传
    if (m_loader.done() && !m_preloadRequested) {
        m_preloadRequested = true;
        auto* songSelect = Kernel::instance().stateManager().getStateAs<SongSelectState>(GameState::SongSelect);
        if (songSelect) {
            m_preloadPaths = songSelect->getGroupImagePaths();
            if (!m_preloadPaths.empty()) {
                renderer::TextureCache::instance().preload(m_preloadPaths);
            } else {
                m_texturesLoaded = true;
            }
        } else {
            m_texturesLoaded = true;
        }
    }

    if (m_preloadRequested && !m_texturesLoaded) {
        auto& cache = renderer::TextureCache::instance();
        bool allReady = true;
        for (const auto& path : m_preloadPaths) {
            if (!cache.isLoaded(path) && !cache.hasFailed(path)) {
                allReady = false;
                break;
            }
        }
        if (allReady) {
            m_texturesLoaded = true;
        }
    }

    if (m_time >= MINIMUM_DURATION) {                        // 品牌动画最短时长
        m_animationDone = true;
    }

    if (m_animationDone && m_loader.done() && m_texturesLoaded && !m_transitionRequested) {
        m_transitionRequested = true;                        // 防止重复返回 MainMenu
        return GameState::MainMenu;
    }

    return GameState::Count;
}

// ══════════════════════════════════════════════════════════════════════════════
//  渲染
// ══════════════════════════════════════════════════════════════════════════════

/// 渲染加载动画与进度条
void BootState::render() {
    renderLoadingAnimation();
    renderProgressBar(m_loader.progress());
}

/// 绘制 "Mi hits" 品牌动画（ImDrawList 逐段路径绘制 + 光谱渐变）
void BootState::renderLoadingAnimation() {
    using namespace ui;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float W = displaySize.x;
    float H = displaySize.y;
    float scale = H / 1080.0f;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // ── 动画时间轴 ──
    // 0 ~ M_DRAW_DURATION: "M" 路径绘制
    // M_DRAW_DURATION ~ M_DRAW_DURATION+I_STEM_DURATION: "i" 竖线
    // ...+I_STEM_DURATION ~ ...+I_DOT_DURATION: "i" 点
    // ...+I_DOT_DURATION ~ MINIMUM_DURATION: "hits" 滑入

    float t = m_time;

    // ── 字体尺寸参数 ──
    float fontSize = 120.0f * scale;    // 字母高度
    float mWidth = fontSize * 0.85f;     // M 的宽度
    float iWidth = fontSize * 0.25f;     // i 的宽度
    float iGap = fontSize * 0.18f;       // M 和 i 之间的间距
    float dotRadius = fontSize * 0.06f;  // i 的点的半径
    float dotGap = fontSize * 0.12f;     // 点与竖线的间距
    float lineWidth = 4.0f * scale;      // 笔画线宽

    // 整体居中偏移（"Mi" 组合的中心）
    float totalWidth = mWidth + iGap + iWidth;
    float startX = (W - totalWidth) / 2.0f;
    float baseY = H * 0.42f; // 垂直偏上

    // ── 光谱渐变颜色 ──
    // 沿路径从青色 → 紫色 → 粉色
    auto spectrumColor = [](float t) -> ImU32 {
        // t: 0→1, 颜色: cyan → purple → pink
        float r, g, b;
        if (t < 0.5f) {
            float s = t * 2.0f;
            r = Theme::CYAN_R + (Theme::PURP_R - Theme::CYAN_R) * s;
            g = Theme::CYAN_G + (Theme::PURP_G - Theme::CYAN_G) * s;
            b = Theme::CYAN_B + (Theme::PURP_B - Theme::CYAN_B) * s;
        } else {
            float s = (t - 0.5f) * 2.0f;
            r = Theme::PURP_R + (Theme::PINK_R - Theme::PURP_R) * s;
            g = Theme::PURP_G + (Theme::PINK_G - Theme::PURP_G) * s;
            b = Theme::PURP_B + (Theme::PINK_B - Theme::PURP_B) * s;
        }
        return IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255),
                         static_cast<int>(b * 255), 255);
    };

    // ══════════════════════════════════════════════════════════════════════
    //  "M" 路径绘制动画
    // ══════════════════════════════════════════════════════════════════════
    if (t > 0.0f) {
        // M 的5个关键点（连续路径：左下→左上→中下→右上→右下）
        ImVec2 mPts[5] = {
            ImVec2(startX,              baseY + fontSize),           // 左下
            ImVec2(startX,              baseY),                      // 左上
            ImVec2(startX + mWidth/2,   baseY + fontSize * 0.65f),  // 中下
            ImVec2(startX + mWidth,     baseY),                      // 右上
            ImVec2(startX + mWidth,     baseY + fontSize),           // 右下
        };

        // 计算各段长度
        float segLen[4];
        float totalLen = 0.0f;
        for (int i = 0; i < 4; ++i) {
            float dx = mPts[i+1].x - mPts[i].x;
            float dy = mPts[i+1].y - mPts[i].y;
            segLen[i] = std::sqrt(dx*dx + dy*dy);
            totalLen += segLen[i];
        }

        // 当前绘制进度（0~1）
        float mProgress = std::min(t / M_DRAW_DURATION, 1.0f);
        float drawLen = mProgress * totalLen;

        // 沿路径逐段绘制
        float accumulated = 0.0f;
        for (int i = 0; i < 4; ++i) {
            if (accumulated >= drawLen) break;

            float segProgress = std::min((drawLen - accumulated) / segLen[i], 1.0f);
            float colorT = (accumulated + segLen[i] * 0.5f) / totalLen;

            ImVec2 p0 = mPts[i];
            ImVec2 p1(mPts[i].x + (mPts[i+1].x - mPts[i].x) * segProgress,
                      mPts[i].y + (mPts[i+1].y - mPts[i].y) * segProgress);

            dl->AddLine(p0, p1, spectrumColor(colorT), lineWidth);

            // 笔尖发光效果
            if (segProgress < 1.0f && mProgress < 1.0f) {
                dl->AddCircleFilled(p1, lineWidth * 2.5f,
                    IM_COL32(255, 255, 255, 120));
            }

            accumulated += segLen[i];
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  "i" 竖线绘制动画
    // ══════════════════════════════════════════════════════════════════════
    float iStartTime = M_DRAW_DURATION;
    float iX = startX + mWidth + iGap;
    float iStemTop = baseY + fontSize * 0.05f;
    float iStemBottom = baseY + fontSize;

    if (t > iStartTime) {
        float iProgress = std::min((t - iStartTime) / I_STEM_DURATION, 1.0f);
        float currentBottom = iStemTop + (iStemBottom - iStemTop) * iProgress;

        dl->AddLine(
            ImVec2(iX, iStemTop),
            ImVec2(iX, currentBottom),
            IM_COL32(static_cast<int>(Theme::CYAN_R * 255),
                      static_cast<int>(Theme::CYAN_G * 255),
                      static_cast<int>(Theme::CYAN_B * 255), 255),
            lineWidth
        );

        // 笔尖发光
        if (iProgress < 1.0f) {
            dl->AddCircleFilled(ImVec2(iX, currentBottom), lineWidth * 2.5f,
                IM_COL32(255, 255, 255, 120));
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  "i" 点动画
    // ══════════════════════════════════════════════════════════════════════
    float dotStartTime = iStartTime + I_STEM_DURATION;
    if (t > dotStartTime) {
        float dotProgress = std::min((t - dotStartTime) / I_DOT_DURATION, 1.0f);
        // 缩放出现：从0放大到正常
        float currentRadius = dotRadius * dotProgress;
        ImVec2 dotCenter(iX, iStemTop - dotGap);

        if (currentRadius > 0.5f) {
            dl->AddCircleFilled(dotCenter, currentRadius,
                IM_COL32(static_cast<int>(Theme::CYAN_R * 255),
                          static_cast<int>(Theme::CYAN_G * 255),
                          static_cast<int>(Theme::CYAN_B * 255), 255));
            // 发光
            dl->AddCircleFilled(dotCenter, currentRadius * 1.8f,
                IM_COL32(static_cast<int>(Theme::CYAN_R * 255),
                          static_cast<int>(Theme::CYAN_G * 255),
                          static_cast<int>(Theme::CYAN_B * 255), 60));
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  "hits" 光谱笔画滑入动画
    // ══════════════════════════════════════════════════════════════════════
    float hitsStartTime = dotStartTime + I_DOT_DURATION;
    if (t > hitsStartTime) {
        float hitsProgress = std::min((t - hitsStartTime) / HITS_SLIDE_DURATION, 1.0f);
        // ease-out 缓动
        float eased = 1.0f - std::pow(1.0f - hitsProgress, 3.0f);

        // "hits" 笔画参数
        float hitsX = iX + iWidth + fontSize * 0.08f;
        float slideOffset = (1.0f - eased) * W * 0.3f; // 从右侧滑入
        float finalX = hitsX + slideOffset;
        float hitsY = baseY + fontSize * 0.15f; // 垂直顶部对齐
        float hitsHeight = fontSize * 0.55f;     // hits 字母高度
        float hitsLineW = 3.0f * scale;          // 笔画线宽

        // 定义 "hits" 各字母的笔画路径（简化为线段集合）
        // 每个字母由若干线段组成：{起点, 终点}
        // 坐标相对于 (finalX, hitsY)
        struct HitStroke { float x0, y0, x1, y1; };

        // h: 竖线 + 弧形
        HitStroke hStrokes[] = {
            {0.00f, 0.00f, 0.00f, 1.00f},  // 左竖线
            {0.00f, 0.55f, 0.35f, 0.55f},  // 横线
            {0.35f, 0.55f, 0.35f, 1.00f},  // 右竖线下
        };
        // i: 竖线 + 点
        HitStroke iStrokes[] = {
            {0.55f, 0.30f, 0.55f, 1.00f},  // 竖线
        };
        // t: 横线 + 竖线
        HitStroke tStrokes[] = {
            {0.70f, 0.30f, 1.05f, 0.30f},  // 横线
            {0.87f, 0.30f, 0.87f, 1.00f},  // 竖线
        };
        // s: 曲线简化为折线
        HitStroke sStrokes[] = {
            {1.25f, 0.70f, 1.10f, 0.55f},  // 上弧
            {1.10f, 0.55f, 1.25f, 0.40f},  // 中间
            {1.25f, 0.40f, 1.10f, 0.30f},  // 下弧
        };

        // 绘制所有笔画，带光谱渐变
        float totalStrokes = 3 + 1 + 2 + 3; // h3 + i1 + t2 + s3 = 9 笔
        int strokeIdx = 0;

        auto drawStrokes = [&](HitStroke* strokes, int count) {
            for (int s = 0; s < count; ++s, ++strokeIdx) {
                float colorT = static_cast<float>(strokeIdx) / totalStrokes;
                ImU32 col = spectrumColor(colorT);

                ImVec2 p0(finalX + strokes[s].x0 * hitsHeight, hitsY + strokes[s].y0 * hitsHeight);
                ImVec2 p1(finalX + strokes[s].x1 * hitsHeight, hitsY + strokes[s].y1 * hitsHeight);
                dl->AddLine(p0, p1, col, hitsLineW);

                // 笔尖发光
                if (hitsProgress < 1.0f && s == count - 1) {
                    dl->AddCircleFilled(p1, hitsLineW * 2.0f,
                        IM_COL32(255, 255, 255, 100));
                }
            }
        };

        drawStrokes(hStrokes, 3);
        drawStrokes(iStrokes, 1);

        // i 的点
        {
            float dotR = hitsHeight * 0.04f;
            ImVec2 dotCenter(finalX + 0.55f * hitsHeight, hitsY + hitsHeight * 0.18f);
            dl->AddCircleFilled(dotCenter, dotR, spectrumColor(4.0f / totalStrokes));
            dl->AddCircleFilled(dotCenter, dotR * 1.8f,
                IM_COL32(static_cast<int>(Theme::CYAN_R * 255),
                          static_cast<int>(Theme::CYAN_G * 255),
                          static_cast<int>(Theme::CYAN_B * 255), 50));
        }

        drawStrokes(tStrokes, 2);
        drawStrokes(sStrokes, 3);
    }
}

/// 绘制底部圆角进度条（青→紫渐变）与百分比文字
void BootState::renderProgressBar(float progress) {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float W = displaySize.x;
    float H = displaySize.y;
    float scale = H / 1080.0f;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // 进度条参数
    float barWidth = W * 0.4f;
    float barHeight = 4.0f * scale;
    float barX = (W - barWidth) / 2.0f;
    float barY = H * 0.85f;
    float barRadius = barHeight * 0.5f;

    // 背景（深灰圆角条）
    dl->AddRectFilled(
        ImVec2(barX, barY),
        ImVec2(barX + barWidth, barY + barHeight),
        IM_COL32(40, 40, 60, 200),
        barRadius
    );

    // 填充（青色渐变）
    float fillWidth = barWidth * std::min(progress, 1.0f);
    if (fillWidth > 1.0f) {
        // 渐变：左侧青色 → 右侧紫色
        int segments = static_cast<int>(fillWidth / 2.0f);
        segments = std::max(segments, 1);
        float segW = fillWidth / segments;

        for (int i = 0; i < segments; ++i) {
            float t = static_cast<float>(i) / segments;
            float r = ui::Theme::CYAN_R + (ui::Theme::PURP_R - ui::Theme::CYAN_R) * t;
            float g = ui::Theme::CYAN_G + (ui::Theme::PURP_G - ui::Theme::CYAN_G) * t;
            float b = ui::Theme::CYAN_B + (ui::Theme::PURP_B - ui::Theme::CYAN_B) * t;

            ImVec2 p0(barX + i * segW, barY);
            ImVec2 p1(barX + (i + 1) * segW, barY + barHeight);
            dl->AddRectFilled(p0, p1,
                IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255),
                          static_cast<int>(b * 255), 255),
                (i == 0 || i == segments - 1) ? barRadius : 0.0f);
        }

        // 前端发光
        dl->AddCircleFilled(
            ImVec2(barX + fillWidth, barY + barHeight * 0.5f),
            barHeight * 2.0f,
            IM_COL32(static_cast<int>(ui::Theme::CYAN_R * 255),
                      static_cast<int>(ui::Theme::CYAN_G * 255),
                      static_cast<int>(ui::Theme::CYAN_B * 255), 60)
        );
    }

    // 百分比文字
    char pctText[16];
    snprintf(pctText, sizeof(pctText), "%d%%", static_cast<int>(progress * 100));
    dl->AddText(ImGui::GetFont(), 14.0f * scale,
        ImVec2(barX + barWidth + 12.0f * scale, barY - 4.0f * scale),
        IM_COL32(140, 140, 165, 200), pctText);
}

} // namespace melody_matrix::core
