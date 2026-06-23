#include "song_select_state.h"
#include "core/kernel.h"
#include "core/state_manager.h"
#include "core/states/playing_state.h"
#include "ui/theme.h"
#include "util/logger.h"
#include "beatmap/beatmap_parser.h"
#include "beatmap/beatmap_builder.h"
#include "platform/file_system.h"

#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace melody_matrix::core {

// ══════════════════════════════════════════════════════════════════════════════
//  响应式布局计算
// ══════════════════════════════════════════════════════════════════════════════

void SongSelectState::computeLayout(float screenW, float screenH) {
    m_ly.W = screenW;
    m_ly.H = screenH;
    m_ly.scale = screenH / 1080.0f;

    m_ly.leftWidth  = std::max(200.0f, screenW * LEFT_RATIO);
    m_ly.rightWidth = screenW - m_ly.leftWidth;
    m_ly.topInfoH   = std::max(80.0f, screenH * 0.222f);   // ≈240 @1080p
    m_ly.bottomBarH = std::max(40.0f, screenH * 0.074f);   // ≈80  @1080p
    m_ly.groupHeaderH = std::max(40.0f, screenH * 0.079f); // ≈85  @1080p
    m_ly.setItemH   = std::max(30.0f, screenH * 0.051f);   // ≈55  @1080p
    m_ly.groupSpacing = std::max(3.0f, screenH * 0.006f);  // ≈6   @1080p
    m_ly.setIndent  = std::max(12.0f, screenH * 0.022f);   // ≈24  @1080p
    m_ly.listIndent = std::max(60.0f, screenW * 0.065f);   // ≈125 @1920p
    m_ly.rightPadding = std::max(8.0f, screenW * 0.01f);   // ≈20  @1920p
    m_ly.maskArcHeight = std::max(20.0f, screenH * 0.04f); // ≈43  @1080p
}

// ══════════════════════════════════════════════════════════════════════════════
//  生命周期
// ══════════════════════════════════════════════════════════════════════════════

void SongSelectState::onEnter() {
    MM_LOG_INFO("SongSelect", "Entering Song Select");

    // 初始化音频引擎（每次进入时初始化，退出时 shutdown）
    if (!m_audio.init()) {
        MM_LOG_ERROR("SongSelect", "Failed to initialize audio engine!");
    }

    m_nextState = GameState::Count;
    m_selectedBeatmap.clear();
    m_modPopupOpen = false;
    m_bgImageGroup = -1;

    if (!m_scanDone) {
        scanBeatmaps();
        // 补加载新图片到全局缓存（已缓存的无开销）
        scanAndPreload();
    }

    // 加载头像
    loadAvatarTexture();

    // 初始化 Mod 列表
    if (m_mods.empty()) {
        m_mods.push_back({"NoFail", "nofail", false, true});
    }

    // 随机选中一组铺面
    if (!m_groups.empty() && m_selectedGroup < 0) {
        m_selectedGroup = rand() % static_cast<int>(m_groups.size());
        m_selectedSet = 0;
        tryLoadGroupImage(m_selectedGroup);
        tryPlayPreview();   // 随机选中后触发预览
    }
}

void SongSelectState::onExit() {
    MM_LOG_INFO("SongSelect", "Exiting Song Select");

    // 淡出并停止所有预览音频，然后关闭引擎
    m_audio.stopWithFade(0.3f);
    m_audio.shutdown();
    m_lastPreviewAudioPath.clear();

    // 恢复 Renderer 默认背景
    Kernel::instance().renderer().setBackgroundPath("");

    // 不再清空纹理缓存 — 图片保留在 TextureCache 中，下次进入时无需重新加载
    m_bgImageGroup = -1;
}

GameState SongSelectState::update(float dt) {
    // 驱动音频引擎（淡入淡出 + 预览循环）
    m_audio.update(dt);

    // 如果选中了铺面，传递给 PlayingState 并转换
    if (!m_selectedBeatmap.empty()) {
        auto* playing = Kernel::instance().stateManager().getStateAs<PlayingState>(GameState::Playing);
        if (playing) {
            playing->setBeatmapFile(m_selectedBeatmap);
            playing->markNeedsReinit();
            // 传递背景图路径
            if (m_selectedGroup >= 0 && m_selectedGroup < static_cast<int>(m_groups.size())) {
                playing->setBackgroundImage(m_groups[m_selectedGroup].imagePath);
            }
        }
        return GameState::Playing;
    }

    // 全屏背景图跟随选中组更新
    if (m_selectedGroup >= 0 && m_bgImageGroup != m_selectedGroup) {
        tryLoadGroupImage(m_selectedGroup);
        m_bgImageGroup = m_selectedGroup;
        // 同步 Renderer 背景图：有谱面背景时使用谱面背景，否则用默认
        auto& renderer = Kernel::instance().renderer();
        if (m_selectedGroup >= 0 && m_selectedGroup < static_cast<int>(m_groups.size())) {
            const auto& group = m_groups[m_selectedGroup];
            // 只有当背景图不是默认的 menu-bg.jpg 时才设置
            if (group.imagePath.find("menu-bg.jpg") == std::string::npos) {
                renderer.setBackgroundPath(group.imagePath);
            } else {
                renderer.setBackgroundPath("");
            }
        }
    }

    return m_nextState;
}

void SongSelectState::render() {
    renderImGuiPanel();
    if (m_modPopupOpen) {
        renderModPopup();
    }

    // ── 右侧面板顶部遮罩（遮挡滚入的列表项）──
    // 底部轮廓：1/4屏宽直线(全高) → 1/5屏宽内弧(余弦过渡) → 剩余直线(薄条至屏幕右侧)
    // 边缘：白色描边 + 内侧蓝色描边
    if (!m_modPopupOpen) {
        ImDrawList* fgDl = ImGui::GetForegroundDrawList();
        float maskL = m_ly.leftWidth;
        float maskR = m_ly.W;
        float topInfoH = m_ly.topInfoH;
        float thinH = topInfoH * 0.20f;
        float W = m_ly.W;

        float seg1End   = W * 0.25f;           // 第一段直线末端
        float arcWidth  = W * 0.20f;            // 弧段宽度(1/5屏宽)
        float seg3Start = seg1End + arcWidth;   // 第三段直线起点

        const int ARC_N = 32;
        const float PI = 3.14159265f;

        // 余弦插值弧线：t∈[0,1], t=0→y=topInfoH, t=1→y=thinH，两端水平切线
        auto arcY = [&](float t) -> float {
            return thinH + (topInfoH - thinH) * (1.0f + cosf(PI * t)) / 2.0f;
        };

        // fgDl 起始位置在弧线区域内（leftWidth ≈ W*0.38, 在 seg1End~seg3Start 之间）
        float tL = std::max(0.0f, std::min(1.0f, (maskL - seg1End) / arcWidth));
        float yL = arcY(tL);

        // ── 填充多边形 ──
        ImVec2 pts[6 + ARC_N];
        int n = 0;
        pts[n++] = ImVec2(maskL, 0);
        pts[n++] = ImVec2(maskR, 0);
        pts[n++] = ImVec2(maskR, thinH);
        pts[n++] = ImVec2(seg3Start, thinH);
        // 弧线从右(t=1)到左(t=tL)，跳过t=1(已添加为seg3Start)
        for (int i = ARC_N - 1; i >= 0; --i) {
            float t = static_cast<float>(i) / ARC_N;
            float x = seg1End + arcWidth * t;
            if (x < maskL) break;
            pts[n++] = ImVec2(x, arcY(t));
        }
        pts[n++] = ImVec2(maskL, yL);
        fgDl->AddConvexPolyFilled(pts, n, IM_COL32(13, 13, 23, 255));

        // ── 底部轮廓白色描边（左→右）──
        ImVec2 edgePts[3 + ARC_N];
        int en = 0;
        edgePts[en++] = ImVec2(maskL, yL);
        // 弧线从左(t=tL)到右(t=1)
        int iStart = static_cast<int>(ceilf(tL * ARC_N));
        for (int i = iStart; i <= ARC_N; ++i) {
            float t = static_cast<float>(i) / ARC_N;
            edgePts[en++] = ImVec2(seg1End + arcWidth * t, arcY(t));
        }
        edgePts[en++] = ImVec2(seg3Start, thinH);
        edgePts[en++] = ImVec2(maskR, thinH);
        fgDl->AddPolyline(edgePts, en, IM_COL32(255, 255, 255, 200), false, m_ly.scale * 2.0f);

        // ── 蓝色内侧描边（向上偏移bOff）──
        float bOff = m_ly.scale * 5.0f;
        ImVec2 bluePts[3 + ARC_N];
        int bn = 0;
        bluePts[bn++] = ImVec2(maskL, yL - bOff);
        for (int i = iStart; i <= ARC_N; ++i) {
            float t = static_cast<float>(i) / ARC_N;
            bluePts[bn++] = ImVec2(seg1End + arcWidth * t, arcY(t) - bOff);
        }
        bluePts[bn++] = ImVec2(seg3Start, thinH - bOff);
        bluePts[bn++] = ImVec2(maskR, thinH - bOff);
        fgDl->AddPolyline(bluePts, bn, IM_COL32(0, 150, 255, 180), false, m_ly.scale * 1.5f);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  铺面扫描
// ══════════════════════════════════════════════════════════════════════════════

void SongSelectState::scanBeatmaps() {
    MM_LOG_INFO("SongSelect", "Scanning beatmaps in: " + m_beatmapDir);

    m_groups.clear();
    std::vector<BeatmapEntry> entries;

    try {
        std::filesystem::path beatmapDir(m_beatmapDir);
        if (!std::filesystem::exists(beatmapDir)) {
            MM_LOG_WARN("SongSelect", "Beatmap directory does not exist: " + m_beatmapDir);
            m_scanDone = true;
            return;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(beatmapDir)) {
            if (!entry.is_regular_file()) continue;

            std::string ext = entry.path().extension().string();
            for (auto& c : ext) c = static_cast<char>(tolower(c));

            if (ext != ".mma" && ext != ".osu") continue;

            std::string filePath = std::filesystem::absolute(entry.path()).string();

            try {
                auto readResult = platform::FileSystem::readFile(filePath);
                if (!readResult.ok()) continue;

                const std::string& content = readResult.value();
                auto parser = beatmap::createParserForFile(filePath);
                beatmap::BeatmapBuilder builder;
                auto parseResult = parser->parse(content, builder);
                if (!parseResult.ok()) continue;

                auto buildResult = builder.build();
                if (!buildResult.ok()) continue;

                const auto& beatmap = buildResult.value();

                BeatmapEntry e;
                e.filePath = filePath;
                e.title = beatmap.meta.title;
                e.artist = beatmap.meta.artist;
                e.creator = beatmap.meta.creator;
                e.version = beatmap.meta.version;
                e.difficulty = beatmap.difficulty.star;
                e.ar = beatmap.difficulty.ar;
                e.od = beatmap.difficulty.od;
                e.hp = beatmap.difficulty.hp;
                e.noteCount = static_cast<int>(beatmap.noteCount());

                if (!beatmap.notes.empty()) {
                    e.duration = static_cast<float>(beatmap.notes.back().time) / 1000.0f;
                }

                e.previewTime = beatmap.meta.previewTime;
                // 音频文件路径：meta.audioFile 是相对于铺面文件所在目录的路径
                if (!beatmap.meta.audioFile.empty()) {
                    auto audioPath = entry.path().parent_path() / beatmap.meta.audioFile;
                    e.audioFilePath = std::filesystem::absolute(audioPath).string();
                }

                // 背景图查找
                auto bgDir = entry.path().parent_path();
                for (const char* bgName : {"background.jpg", "background.png", "bg.jpg", "bg.png"}) {
                    auto bgPath = bgDir / bgName;
                    if (platform::FileSystem::fileExists(bgPath.string())) {
                        e.imagePath = std::filesystem::absolute(bgPath).string();
                        break;
                    }
                }
                if (e.imagePath.empty()) {
                    e.imagePath = std::filesystem::absolute("assets/textures/menu-bg.jpg").string();
                }

                entries.push_back(e);
            } catch (const std::exception& e) {
                MM_LOG_WARN("SongSelect", "Failed to parse beatmap %s: %s",
                            filePath.c_str(), e.what());
            }
        }
    } catch (const std::exception& e) {
        MM_LOG_WARN("SongSelect", "Failed to scan beatmaps: %s", e.what());
    }

    // 如果没有扫描到任何铺面，保留 demo 数据
    if (entries.empty()) {
        BeatmapEntry e;
        e.filePath = "assets/beatmaps/demo.mma";
        e.title = "Neon Dreams";
        e.artist = "Melody Matrix Demo";
        e.creator = "System";
        e.version = "Normal";
        e.difficulty = 3.5f;
        e.ar = 7.0f;
        e.od = 5.0f;
        e.hp = 4.0f;
        e.noteCount = 18;
        e.duration = 10.0f;
        e.imagePath = "assets/textures/menu-bg.jpg";
        entries.push_back(e);
    }

    // 按标题排序，同标题按难度排
    std::sort(entries.begin(), entries.end(),
        [](const BeatmapEntry& a, const BeatmapEntry& b) {
            if (a.title != b.title) return a.title < b.title;
            return a.difficulty < b.difficulty;
        });

    // ── 按 title+artist 分组 ──
    for (auto& e : entries) {
        if (!m_groups.empty() && m_groups.back().title == e.title && m_groups.back().artist == e.artist) {
            m_groups.back().sets.push_back(std::move(e));
        } else {
            BeatmapGroup g;
            g.title = e.title;
            g.artist = e.artist;
            g.imagePath = e.imagePath;
            g.sets.push_back(std::move(e));
            m_groups.push_back(std::move(g));
        }
    }

    m_scanDone = true;
    MM_LOG_INFO("SongSelect", "Found %d beatmap groups", static_cast<int>(m_groups.size()));
}

// ══════════════════════════════════════════════════════════════════════════════
//  辅助
// ══════════════════════════════════════════════════════════════════════════════

const SongSelectState::BeatmapEntry* SongSelectState::getSelectedSet() const {
    if (m_selectedGroup < 0 || m_selectedGroup >= static_cast<int>(m_groups.size())) return nullptr;
    const auto& group = m_groups[m_selectedGroup];
    if (m_selectedSet < 0 || m_selectedSet >= static_cast<int>(group.sets.size())) return nullptr;
    return &group.sets[m_selectedSet];
}

void SongSelectState::loadAvatarTexture() {
    if (!m_avatarTexture.valid()) {
        const char* paths[] = {
            "res/icon.png",
            "../res/icon.png",
            "../../res/icon.png"
        };
        bool loaded = false;
        for (const char* p : paths) {
            if (m_avatarTexture.loadFromFile(p, false)) {
                MM_LOG_INFO("SongSelect", "Avatar loaded from: %s", p);
                loaded = true;
                break;
            }
        }
        if (!loaded) {
            MM_LOG_WARN("SongSelect", "Failed to load avatar texture from all paths");
        }
    }
}

void SongSelectState::tryLoadGroupImage(int groupIndex) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(m_groups.size())) return;

    const auto& group = m_groups[groupIndex];
    if (group.imagePath.empty()) return;

    // 通过全局纹理缓存加载（已缓存则直接返回，无需磁盘 I/O）
    renderer::TextureCache::instance().load(group.imagePath, false);
}

void SongSelectState::unloadUnusedImages() {
    auto paths = getGroupImagePaths();
    renderer::TextureCache::instance().unloadDistant(paths, m_selectedGroup, 5);
}

// ══════════════════════════════════════════════════════════════════════════════
//  主面板渲染
// ══════════════════════════════════════════════════════════════════════════════

void SongSelectState::renderImGuiPanel() {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float W = displaySize.x;
    float H = displaySize.y;

    // 每帧计算响应式布局
    computeLayout(W, H);

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar;

    ImGui::Begin("##SongSelect", nullptr, flags);

    // ── 全屏背景图已由 Renderer::renderBackground() 绘制，此处不再重复渲染 ──

    // ── 顶部实心遮罩（左面板区域 + 弧形过渡区）──
    {
        ImDrawList* bgDl = ImGui::GetWindowDrawList();
        float maskR = W;
        float topInfoH = m_ly.topInfoH;
        float thinH = topInfoH * 0.20f;

        float seg1End   = W * 0.25f;
        float arcWidth  = W * 0.20f;
        float seg3Start = seg1End + arcWidth;

        const int ARC_N = 32;
        const float PI = 3.14159265f;

        auto arcY = [&](float t) -> float {
            return thinH + (topInfoH - thinH) * (1.0f + cosf(PI * t)) / 2.0f;
        };

        // ── 填充多边形 ──
        ImVec2 pts[4 + ARC_N + 2];
        int n = 0;
        pts[n++] = ImVec2(0, 0);
        pts[n++] = ImVec2(maskR, 0);
        pts[n++] = ImVec2(maskR, thinH);
        pts[n++] = ImVec2(seg3Start, thinH);
        for (int i = ARC_N - 1; i >= 0; --i) {
            float t = static_cast<float>(i) / ARC_N;
            pts[n++] = ImVec2(seg1End + arcWidth * t, arcY(t));
        }
        pts[n++] = ImVec2(0, topInfoH);
        bgDl->AddConvexPolyFilled(pts, n, IM_COL32(13, 13, 23, 255));

        // ── 底部轮廓白色描边（左→右）──
        ImVec2 edgePts[2 + ARC_N + 2];
        int en = 0;
        edgePts[en++] = ImVec2(0, topInfoH);
        edgePts[en++] = ImVec2(seg1End, topInfoH);
        for (int i = 1; i <= ARC_N; ++i) {
            float t = static_cast<float>(i) / ARC_N;
            edgePts[en++] = ImVec2(seg1End + arcWidth * t, arcY(t));
        }
        edgePts[en++] = ImVec2(maskR, thinH);
        bgDl->AddPolyline(edgePts, en, IM_COL32(255, 255, 255, 200), false, m_ly.scale * 2.0f);

        // ── 蓝色内侧描边（向上偏移bOff）──
        float bOff = m_ly.scale * 5.0f;
        ImVec2 bluePts[2 + ARC_N + 2];
        int bn = 0;
        bluePts[bn++] = ImVec2(0, topInfoH - bOff);
        bluePts[bn++] = ImVec2(seg1End, topInfoH - bOff);
        for (int i = 1; i <= ARC_N; ++i) {
            float t = static_cast<float>(i) / ARC_N;
            bluePts[bn++] = ImVec2(seg1End + arcWidth * t, arcY(t) - bOff);
        }
        bluePts[bn++] = ImVec2(maskR, thinH - bOff);
        bgDl->AddPolyline(bluePts, bn, IM_COL32(0, 150, 255, 180), false, m_ly.scale * 1.5f);
    }

    renderLeftPanel(m_ly.leftWidth, H);
    renderRightPanel(m_ly.leftWidth, m_ly.rightWidth, H);

    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
//  左侧面板
// ══════════════════════════════════════════════════════════════════════════════

void SongSelectState::renderLeftPanel(float panelWidth, float panelHeight) {
    using namespace ui;

    ImGui::SetCursorPos(ImVec2(0, 0));

    ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m_ly.leftWidth * 0.05f, m_ly.H * 0.015f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        ImVec4(ui::Theme::BG_R, ui::Theme::BG_G, ui::Theme::BG_B, 0.0f));

    ImGui::BeginChild("##LeftPanel", ImVec2(panelWidth, panelHeight), false, childFlags);

    // ── 顶部区域 — 铺面详细信息（TOP_INFO_HEIGHT）──
    {
        float infoY = m_ly.topInfoH * 0.1f;
        ImGui::SetCursorPosY(infoY);

        const BeatmapEntry* sel = getSelectedSet();
        if (sel) {
            // 歌名
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 1.0f));
            ImGui::SetWindowFontScale(1.8f * m_ly.scale);
            ImGui::Text("%s", sel->title.c_str());
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(1.0f);

            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.82f, 1.0f));
            ImGui::Text("Artist: %s", sel->artist.c_str());
            ImGui::Text("Creator: %s", sel->creator.c_str());
            ImGui::PopStyleColor();

            ImGui::Spacing();

            int mins = static_cast<int>(sel->duration) / 60;
            int secs = static_cast<int>(sel->duration) % 60;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.7f, 1.0f));
            ImGui::Text("Length: %d:%02d   Notes: %d", mins, secs, sel->noteCount);
            ImGui::PopStyleColor();

            ImGui::Spacing();

            // AR / OD / HP
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(ui::Theme::PURP_R, ui::Theme::PURP_G, ui::Theme::PURP_B, 0.9f));
            ImGui::Text("AR"); ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));
            ImGui::Text("%.1f", sel->ar);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 16);

            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(ui::Theme::PINK_R, ui::Theme::PINK_G, ui::Theme::PINK_B, 0.9f));
            ImGui::Text("OD"); ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));
            ImGui::Text("%.1f", sel->od);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 16);

            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.9f));
            ImGui::Text("HP"); ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));
            ImGui::Text("%.1f", sel->hp);
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();

            ImGui::Spacing();

            // 难度名称 + 星级
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(ui::Theme::PINK_R, ui::Theme::PINK_G, ui::Theme::PINK_B, 0.9f));
            ImGui::Text("[%s]", sel->version.c_str());
            ImGui::SameLine(0, 10);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255, 200, 50, 255));
            ImGui::Text("%.1f", sel->difficulty);
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.5f, 0.6f));
            ImGui::SetWindowFontScale(1.2f * m_ly.scale);
            ImGui::Text("Select a beatmap");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
        }
    }

    // ── 排行榜区域 ──
    ImGui::SetCursorPosY(m_ly.topInfoH);

    {
        const char* modeLabels[] = {"Local Ranking", "Friend Ranking"};
        int currentMode = static_cast<int>(m_leaderboardMode);

        ImGui::PushItemWidth(panelWidth - panelWidth * 0.08f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
            ImVec4(ui::Theme::BG_R + 0.06f, ui::Theme::BG_G + 0.06f, ui::Theme::BG_B + 0.08f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
            ImVec4(ui::Theme::BG_R + 0.1f, ui::Theme::BG_G + 0.1f, ui::Theme::BG_B + 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
            ImVec4(ui::Theme::BG_R + 0.12f, ui::Theme::BG_G + 0.12f, ui::Theme::BG_B + 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 1.0f));

        if (ImGui::Combo("##LeaderboardMode", &currentMode, modeLabels, 2)) {
            m_leaderboardMode = static_cast<LeaderboardMode>(currentMode);
        }

        ImGui::PopStyleColor(4);
        ImGui::PopItemWidth();
    }

    ImGui::Spacing();

    {
        float lbHeight = panelHeight - m_ly.topInfoH - m_ly.bottomBarH - m_ly.H * 0.074f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
            ImVec4(ui::Theme::BG_R, ui::Theme::BG_G, ui::Theme::BG_B, 0.0f));
        ImGui::BeginChild("##Leaderboard", ImVec2(panelWidth - panelWidth * 0.08f, lbHeight), false);

        if (m_leaderboardMode == LeaderboardMode::Local) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.65f, 1.0f));
            ImGui::Text("No local scores yet");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.65f, 0.6f));
            ImGui::Text("Friend ranking coming soon...");
            ImGui::PopStyleColor();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── 底部栏 — BACK / RANDOM / MOD ──
    float bottomY = panelHeight - m_ly.bottomBarH;
    ImGui::SetCursorPosY(bottomY);
    ImGui::Separator();
    ImGui::Spacing();

    float btnHeight = m_ly.bottomBarH * 0.5f;
    float smallBtnWidth = m_ly.leftWidth * 0.19f;
    float rowY = bottomY + m_ly.bottomBarH * 0.2f;

    float btnPadX = m_ly.leftWidth * 0.05f;

    // BACK 按钮
    ImGui::SetCursorPos(ImVec2(btnPadX, rowY));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.3f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.4f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.45f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.8f, 1.0f));
    if (ImGui::Button("BACK", ImVec2(smallBtnWidth, btnHeight))) {
        m_selectedGroup = -1;
        m_selectedSet = -1;
        m_nextState = GameState::MainMenu;
    }
    ImGui::PopStyleColor(4);

    // RANDOM 按钮
    float rightBtnX = btnPadX + smallBtnWidth + m_ly.leftWidth * 0.03f;
    ImGui::SetCursorPos(ImVec2(rightBtnX, rowY));
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 1.0f));
    if (ImGui::Button("RANDOM", ImVec2(smallBtnWidth, btnHeight))) {
        if (!m_groups.empty()) {
            int newIdx = rand() % static_cast<int>(m_groups.size());
            if (m_groups.size() > 1) {
                while (newIdx == m_selectedGroup) {
                    newIdx = rand() % static_cast<int>(m_groups.size());
                }
            }
            m_selectedGroup = newIdx;
            m_selectedSet = 0;
            m_scrollToSelected = true;
            tryLoadGroupImage(m_selectedGroup);
            tryPlayPreview();   // RANDOM 切换后触发预览
        }
    }
    ImGui::PopStyleColor(4);

    // MOD 按钮
    float modBtnX = rightBtnX + smallBtnWidth + m_ly.leftWidth * 0.02f;
    ImGui::SetCursorPos(ImVec2(modBtnX, rowY));
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 1.0f));
    if (ImGui::Button("MOD", ImVec2(smallBtnWidth, btnHeight))) {
        m_modPopupOpen = !m_modPopupOpen;
    }
    ImGui::PopStyleColor(4);

    // 活跃 Mod 指示
    bool anyModActive = false;
    for (const auto& mod : m_mods) {
        if (mod.active) { anyModActive = true; break; }
    }
    if (anyModActive) {
        ImGui::SameLine(modBtnX + smallBtnWidth + m_ly.scale * 4);
        ImGui::SetCursorPosY(rowY + m_ly.scale * 12);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 dotPos = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(dotPos.x, dotPos.y), m_ly.scale * 4.0f,
                            IM_COL32(255, 0, 110, 255));
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg
    ImGui::PopStyleVar();   // WindowPadding
}

// ══════════════════════════════════════════════════════════════════════════════
//  右侧面板 — 分组铺面列表
// ══════════════════════════════════════════════════════════════════════════════

void SongSelectState::renderRightPanel(float panelX, float panelWidth, float panelHeight) {
    using namespace ui;

    ImGui::SetCursorPos(ImVec2(panelX, 0));

    ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m_ly.rightPadding, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        ImVec4(ui::Theme::BG_R, ui::Theme::BG_G, ui::Theme::BG_B, 0.0f));

    ImGui::BeginChild("##RightPanel", ImVec2(panelWidth, panelHeight), false, childFlags);

    // ── 可滚动铺面列表（右侧缩进 + 右侧无边距到达屏幕右缘）──
    float listWidth = m_ly.rightWidth - m_ly.listIndent - m_ly.rightPadding;
    float listHeight = panelHeight - m_ly.bottomBarH;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + m_ly.listIndent);
    ImGui::BeginChild("##BeatmapList", ImVec2(listWidth, listHeight), false,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // 顶部占位：让首项紧贴遮罩底边（薄条高度）
    float thinH = m_ly.topInfoH * 0.20f;
    ImGui::Dummy(ImVec2(0, thinH));

    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int g = 0; g < static_cast<int>(m_groups.size()); ++g) {
        const auto& group = m_groups[g];
        bool isSelectedGroup = (g == m_selectedGroup);

        // 可见项加载缩略图
        tryLoadGroupImage(g);

        ImGui::PushID(g);

        // ════════════════════════════════════════════════════
        //  组头（歌曲标题 + 艺术家 + 缩略图）
        // ════════════════════════════════════════════════════
        ImVec2 headerPos = ImGui::GetCursorScreenPos();
        float headerWidth = ImGui::GetContentRegionAvail().x;

        // 组头背景
        if (isSelectedGroup) {
            // 选中组：cover 模式背景
            auto* imgTex = renderer::TextureCache::instance().get(group.imagePath);
            if (imgTex && imgTex->valid()) {
                float imgW = static_cast<float>(imgTex->width());
                float imgH = static_cast<float>(imgTex->height());
                float imgAspect = imgW / imgH;
                float headerAspect = headerWidth / m_ly.groupHeaderH;

                float uvX0, uvY0, uvX1, uvY1;
                if (imgAspect > headerAspect) {
                    float vis = headerAspect / imgAspect;
                    float m = (1.0f - vis) * 0.5f;
                    uvX0 = m; uvX1 = 1.0f - m;
                    uvY0 = 1.0f; uvY1 = 0.0f;
                } else {
                    float vis = imgAspect / headerAspect;
                    float m = (1.0f - vis) * 0.5f;
                    uvX0 = 0.0f; uvX1 = 1.0f;
                    uvY0 = 1.0f - m; uvY1 = m;
                }

                dl->AddImage(
                    (ImTextureID)(intptr_t)imgTex->textureId(),
                    headerPos,
                    ImVec2(headerPos.x + headerWidth, headerPos.y + m_ly.groupHeaderH),
                    ImVec2(uvX0, uvY0), ImVec2(uvX1, uvY1),
                    IM_COL32(255, 255, 255, 60)
                );
                dl->AddRectFilled(
                    headerPos,
                    ImVec2(headerPos.x + headerWidth, headerPos.y + m_ly.groupHeaderH),
                    IM_COL32(26, 26, 46, 160)
                );
            } else {
                dl->AddRectFilled(
                    headerPos,
                    ImVec2(headerPos.x + headerWidth, headerPos.y + m_ly.groupHeaderH),
                    IM_COL32(179, 0, 255, 40)
                );
            }

            // 选中边框
            dl->AddRect(
                headerPos,
                ImVec2(headerPos.x + headerWidth, headerPos.y + m_ly.groupHeaderH),
                IM_COL32(179, 0, 255, 180), m_ly.scale * 6.0f, 0, m_ly.scale * 2.0f
            );
        } else {
            // 未选中组：深色背景
            dl->AddRectFilled(
                headerPos,
                ImVec2(headerPos.x + headerWidth, headerPos.y + m_ly.groupHeaderH),
                IM_COL32(30, 30, 48, 200)
            );
        }

        // 组头可点击区域
        ImGui::InvisibleButton("##groupHeader", ImVec2(headerWidth, m_ly.groupHeaderH));

        if (ImGui::IsItemClicked()) {
            m_selectedGroup = g;
            m_selectedSet = 0;  // 自动选中第一个 set
            m_scrollToSelected = true;
            tryLoadGroupImage(g);
            tryPlayPreview();   // 组头点击后触发预览
        }

        // ── 组头缩略图 ──
        float thumbPad = m_ly.groupHeaderH * 0.1f;   // 边距约为组头的10%
        float thumbSize = m_ly.groupHeaderH - thumbPad * 2;
        float thumbX = headerPos.x + thumbPad;
        float thumbY = headerPos.y + thumbPad;

        {
            auto* imgTex = renderer::TextureCache::instance().get(group.imagePath);
            if (imgTex && imgTex->valid()) {
                float imgW = static_cast<float>(imgTex->width());
                float imgH = static_cast<float>(imgTex->height());
                float imgAspect = imgW / imgH;

                float tDrawW, tDrawH;
                if (1.0f > imgAspect) {
                    tDrawH = thumbSize;
                    tDrawW = tDrawH * imgAspect;
                } else {
                    tDrawW = thumbSize;
                    tDrawH = tDrawW / imgAspect;
                }

                float tOffX = thumbX + (thumbSize - tDrawW) * 0.5f;
                float tOffY = thumbY + (thumbSize - tDrawH) * 0.5f;

                dl->AddImage(
                    (ImTextureID)(intptr_t)imgTex->textureId(),
                    ImVec2(tOffX, tOffY),
                    ImVec2(tOffX + tDrawW, tOffY + tDrawH),
                    ImVec2(0, 1), ImVec2(1, 0),
                    IM_COL32(255, 255, 255, 200)
                );
            } else {
                dl->AddRectFilled(
                    ImVec2(thumbX, thumbY),
                    ImVec2(thumbX + thumbSize, thumbY + thumbSize),
                    IM_COL32(50, 50, 70, 255), m_ly.scale * 4.0f
                );
            }
        }

        // ── 组头文字 ──
        float textX = headerPos.x + thumbPad + thumbSize + thumbPad;
        float textY = headerPos.y + thumbPad;

        dl->AddText(ImGui::GetFont(),
            (isSelectedGroup ? 20.0f : 17.0f) * m_ly.scale * ImGui::GetIO().FontGlobalScale,
            ImVec2(textX, textY),
            isSelectedGroup ? IM_COL32(0, 255, 245, 255) : IM_COL32(240, 240, 250, 255),
            group.title.c_str());

        float subY = textY + m_ly.groupHeaderH * (isSelectedGroup ? 0.33f : 0.28f);

        // 艺术家 + set 数量
        char subText[128];
        snprintf(subText, sizeof(subText), "%s  |  %d sets", group.artist.c_str(),
                 static_cast<int>(group.sets.size()));
        dl->AddText(ImGui::GetFont(), 14.0f * m_ly.scale * ImGui::GetIO().FontGlobalScale,
            ImVec2(textX, subY),
            IM_COL32(140, 140, 165, 255),
            subText);

        // 难度范围（右侧）
        if (!group.sets.empty()) {
            float minDiff = group.sets.front().difficulty;
            float maxDiff = group.sets.back().difficulty;
            char rangeText[64];
            if (minDiff == maxDiff) {
                snprintf(rangeText, sizeof(rangeText), "%.1f", minDiff);
            } else {
                snprintf(rangeText, sizeof(rangeText), "%.1f~%.1f", minDiff, maxDiff);
            }
            float rangeX = headerPos.x + headerWidth - headerWidth * 0.15f;
            dl->AddText(ImGui::GetFont(), 14.0f * m_ly.scale * ImGui::GetIO().FontGlobalScale,
                ImVec2(rangeX, headerPos.y + m_ly.groupHeaderH * 0.5f - m_ly.scale * 8),
                IM_COL32(255, 200, 50, 255),
                rangeText);
        }

        // ════════════════════════════════════════════════════
        //  子难度 set 列表（仅选中组展开显示）
        // ════════════════════════════════════════════════════
        if (isSelectedGroup) {
            for (int s = 0; s < static_cast<int>(group.sets.size()); ++s) {
                const auto& set = group.sets[s];
                bool isSelectedSet = (s == m_selectedSet);

                ImGui::PushID(s);

                ImVec2 setPos = ImGui::GetCursorScreenPos();
                float setWidth = headerWidth - m_ly.setIndent;
                ImVec2 setDrawPos(setPos.x + m_ly.setIndent, setPos.y);

                // set 背景
                if (isSelectedSet) {
                    dl->AddRectFilled(
                        setDrawPos,
                        ImVec2(setDrawPos.x + setWidth, setDrawPos.y + m_ly.setItemH),
                        IM_COL32(179, 0, 255, 60)
                    );
                    dl->AddRect(
                        setDrawPos,
                        ImVec2(setDrawPos.x + setWidth, setDrawPos.y + m_ly.setItemH),
                        IM_COL32(179, 0, 255, 180), m_ly.scale * 4.0f, 0, m_ly.scale * 1.5f
                    );
                } else {
                    dl->AddRectFilled(
                        setDrawPos,
                        ImVec2(setDrawPos.x + setWidth, setDrawPos.y + m_ly.setItemH),
                        IM_COL32(20, 20, 35, 180)
                    );
                }

                // set 可点击区域（含缩进空间）
                ImGui::InvisibleButton("##set", ImVec2(setWidth + m_ly.setIndent, m_ly.setItemH));

                if (ImGui::IsItemClicked()) {
                    m_selectedSet = s;
                    tryPlayPreview();   // set 单击后触发预览
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    m_selectedSet = s;
                    m_selectedBeatmap = set.filePath;
                    tryPlayPreview();   // 双击时也触发（防重复逻辑在内部）
                }

                // set 文字
                float setTextX = setDrawPos.x + m_ly.setItemH * 0.3f;
                float setTextY = setDrawPos.y + (m_ly.setItemH - m_ly.setItemH * 0.35f) * 0.5f;

                // 难度名称
                dl->AddText(ImGui::GetFont(), 15.0f * m_ly.scale * ImGui::GetIO().FontGlobalScale,
                    ImVec2(setTextX, setTextY),
                    isSelectedSet ? IM_COL32(0, 255, 245, 255) : IM_COL32(200, 200, 215, 255),
                    set.version.c_str());

                // 星级（右侧）
                char starText[32];
                snprintf(starText, sizeof(starText), "%.1f", set.difficulty);
                float starX = setDrawPos.x + setWidth - setWidth * 0.12f;
                dl->AddText(ImGui::GetFont(), 14.0f * m_ly.scale * ImGui::GetIO().FontGlobalScale,
                    ImVec2(starX, setTextY),
                    IM_COL32(255, 200, 50, 255),
                    starText);

                ImGui::PopID();
            }
        }

        // 组间距
        ImGui::Dummy(ImVec2(0, m_ly.groupSpacing));
        ImGui::PopID();
    }

    // 滚动到选中项
    if (m_scrollToSelected && m_selectedGroup >= 0) {
        ImGui::SetScrollHereY(0.3f);
        m_scrollToSelected = false;
    }

    ImGui::EndChild();

    // ── 右下角 — 个人信息 + START 按钮 ──
    float startBtnWidth = m_ly.rightWidth * 0.17f;
    float startBtnHeight = m_ly.bottomBarH * 0.63f;
    float startBtnX = panelWidth - m_ly.rightPadding * 2 - startBtnWidth;
    float startBtnY = panelHeight - m_ly.bottomBarH + m_ly.bottomBarH * 0.19f;

    {
        float avatarSize = m_ly.bottomBarH * 0.5f;
        float infoBlockLeft = m_ly.rightPadding;

        ImGui::SetCursorPos(ImVec2(infoBlockLeft, startBtnY + (startBtnHeight - avatarSize) * 0.5f));

        if (m_avatarTexture.valid()) {
            ImGui::Image(
                (ImTextureID)(intptr_t)m_avatarTexture.textureId(),
                ImVec2(avatarSize, avatarSize),
                ImVec2(0, 1), ImVec2(1, 0),
                ImVec4(1, 1, 1, 1),
                ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.6f)
            );
        } else {
            ImDrawList* avDl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 center = ImVec2(pos.x + avatarSize * 0.5f, pos.y + avatarSize * 0.5f);
            avDl->AddCircleFilled(center, avatarSize * 0.5f, IM_COL32(50, 50, 70, 255));
            avDl->AddCircle(center, avatarSize * 0.5f, IM_COL32(0, 255, 245, 120), 0, m_ly.scale * 2.0f);
            avDl->AddText(ImGui::GetFont(), 20.0f * m_ly.scale, ImVec2(center.x - m_ly.scale * 5, center.y - m_ly.scale * 10),
                        IM_COL32(0, 255, 245, 200), "?");
            ImGui::Dummy(ImVec2(avatarSize, avatarSize));
        }

        ImGui::SameLine(0, m_ly.scale * 10);
        ImGui::SetCursorPosY(startBtnY + (startBtnHeight - m_ly.scale * 20) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.9f, 1.0f));
        ImGui::SetWindowFontScale(1.2f * m_ly.scale);
        ImGui::Text("Mihits");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
    }

    // START 按钮
    ImGui::SetCursorPos(ImVec2(startBtnX, startBtnY));

    const BeatmapEntry* selSet = getSelectedSet();
    bool canPlay = selSet != nullptr;

    if (canPlay) {
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.3f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.22f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.15f, 0.22f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.22f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.5f, 0.5f));
    }

    ImGui::SetWindowFontScale(1.4f);
    if (ImGui::Button("START", ImVec2(startBtnWidth, startBtnHeight))) {
        if (canPlay) {
            m_selectedBeatmap = selSet->filePath;
        }
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor(4);

    ImGui::EndChild();

    ImGui::PopStyleColor(); // ChildBg
    ImGui::PopStyleVar();   // WindowPadding
}

// ══════════════════════════════════════════════════════════════════════════════
//  Mod 弹窗
// ══════════════════════════════════════════════════════════════════════════════

void SongSelectState::renderModPopup() {
    using namespace ui;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // 半透明遮罩
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);

        ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoTitleBar |
                                        ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoNavFocus |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus |
                                        ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_NoScrollbar;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.5f));
        ImGui::Begin("##ModOverlay", nullptr, overlayFlags);
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // 弹窗
    float popupWidth = m_ly.scale * 400;
    float popupHeight = m_ly.scale * 500;
    float popupX = (displaySize.x - popupWidth) * 0.5f;
    float popupY = (displaySize.y - popupHeight) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(popupX, popupY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, m_ly.scale * 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, m_ly.scale * 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m_ly.scale * 24, m_ly.scale * 20));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
        ImVec4(ui::Theme::BG_R + 0.04f, ui::Theme::BG_G + 0.04f, ui::Theme::BG_B + 0.06f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border,
        ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 0.6f));

    ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("##ModPopup", nullptr, popupFlags);

    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 1.0f));
    ImGui::SetWindowFontScale(2.0f * m_ly.scale);
    ImGui::Text("MODS");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("##ModList", ImVec2(popupWidth - m_ly.scale * 48, popupHeight - m_ly.scale * 120), true);

    if (m_mods.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.65f, 0.7f));
        ImGui::Text("No mods available");
        ImGui::PopStyleColor();
    } else {
        for (int i = 0; i < static_cast<int>(m_mods.size()); ++i) {
            auto& mod = m_mods[i];
            ImGui::PushID(i);

            if (mod.active) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg,
                    ImVec4(ui::Theme::PURP_R, ui::Theme::PURP_G, ui::Theme::PURP_B, 0.15f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_ChildBg,
                    ImVec4(ui::Theme::BG_R + 0.03f, ui::Theme::BG_G + 0.03f, ui::Theme::BG_B + 0.04f, 0.8f));
            }

            ImGui::BeginChild("##modItem", ImVec2(0, m_ly.scale * 60), true,
                              ImGuiWindowFlags_NoScrollbar);

            if (mod.active) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(ui::Theme::CYAN_R, ui::Theme::CYAN_G, ui::Theme::CYAN_B, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.85f, 1.0f));
            }
            ImGui::SetWindowFontScale(1.2f);
            ImGui::Text("%s", mod.name.c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            if (!mod.implemented) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.55f, 0.6f));
                ImGui::Text("[Coming Soon]");
                ImGui::PopStyleColor();
            }

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.65f, 1.0f));
            ImGui::Text("%s", mod.active ? "Active" : "Inactive");
            ImGui::PopStyleColor();

            ImGui::EndChild();
            ImGui::PopStyleColor(); // ChildBg

            if (ImGui::IsItemClicked() && mod.implemented) {
                mod.active = !mod.active;
            }

            ImGui::Spacing();
            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    ImGui::Spacing();

    float closeBtnWidth = popupWidth - 48;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.3f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.4f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.45f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.8f, 1.0f));
    if (ImGui::Button("CLOSE", ImVec2(closeBtnWidth, 40))) {
        m_modPopupOpen = false;
    }
    ImGui::PopStyleColor(4);

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    // 点击遮罩关闭
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x < popupX || mouse.x > popupX + popupWidth ||
            mouse.y < popupY || mouse.y > popupY + popupHeight) {
            m_modPopupOpen = false;
        }
    }
}

// ============================================================
//  tryPlayPreview —— 尝试播放当前选中 set 的预览音频
// ============================================================
void SongSelectState::tryPlayPreview() {
    const auto* sel = getSelectedSet();
    if (!sel) return;

    // 没有音频路径时静默返回
    if (sel->audioFilePath.empty()) return;

    // 同一首歌（音频路径相同）不重启，继续播放
    if (sel->audioFilePath == m_lastPreviewAudioPath) return;

    // 路径不同 → 播放新音频
    m_lastPreviewAudioPath = sel->audioFilePath;
    m_audio.playPreview(sel->audioFilePath, sel->previewTime, 0.3f, 60000);
}

// ══════════════════════════════════════════════════════════════════════════════
//  预加载与路径查询
// ══════════════════════════════════════════════════════════════════════════════

void SongSelectState::scanAndPreload() {
    if (!m_scanDone) {
        scanBeatmaps();
    }

    auto paths = getGroupImagePaths();
    if (!paths.empty()) {
        renderer::TextureCache::instance().preload(paths);
    }

    MM_LOG_INFO("SongSelect", "scanAndPreload complete: %zu groups, %zu unique image paths",
                m_groups.size(), paths.size());
}

std::vector<std::string> SongSelectState::getGroupImagePaths() const {
    std::vector<std::string> paths;
    paths.reserve(m_groups.size());
    for (const auto& group : m_groups) {
        if (!group.imagePath.empty()) {
            paths.push_back(group.imagePath);
        }
    }
    return paths;
}

} // namespace melody_matrix::core
