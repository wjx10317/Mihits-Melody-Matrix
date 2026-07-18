// ============================================================
// song_select_state.cpp — 选歌状态实现
//
// 职责：
//   - 扫描 assets/beatmaps/ 构建分组铺面列表
//   - 左右分割 UI：排行榜 + 铺面列表
//   - Mod 选择、预览音频、删除铺面
//   - 选中铺面后过渡到 PlayingState
// ============================================================
#include "song_select_state.h"
#include "core/kernel.h"
#include "core/state_manager.h"
#include "core/states/playing_state.h"
#include "core/states/main_menu_state.h"
#include "beatmap/mma_serializer.h"
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
#include <optional>
#include <unordered_set>

namespace melody_matrix::core {

// ══════════════════════════════════════════════════════════════════════════════
//  响应式布局计算
// ══════════════════════════════════════════════════════════════════════════════

/// 根据屏幕尺寸计算左右面板宽度、行高等响应式参数
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

/// 进入选歌状态：初始化预览音频、扫描谱面、Mod 列表
void SongSelectState::onEnter() {
    MM_LOG_INFO("SongSelect", "Entering Song Select");

    if (!m_audio.init()) {                                   // 选歌专用音频引擎（与 Playing 分离）
        MM_LOG_ERROR("SongSelect", "Failed to initialize audio engine!");
    } else {
        m_audio.loadSfx();
    }

    m_nextState = GameState::Count;
    m_selectedBeatmap.clear();                               // 清空“待开始”路径
    m_modPopupOpen = false;
    m_bgImageGroup = -1;

    if (!m_scanDone) {                                         // 首次进入或 markNeedsRescan
        scanBeatmaps();                                        // 扫描并按组 requestLoad 背景图
    }

    loadAvatarTexture();

    if (m_mods.empty()) {                                      // 初始化可选 mod
        m_mods.push_back({"NoFail", "nofail", false, true});
        m_mods.push_back({"Autoplay", "autoplay", false, true});
        m_mods.push_back({"osu mod", "osu", false, true});  // Z/X 全列判定（osu 转换谱测试）
    }

    if (!m_groups.empty() && m_selectedGroup < 0) {          // 首次进入随机选歌
        m_selectedGroup = rand() % static_cast<int>(m_groups.size());
        m_selectedSet = 0;
    }

    // 从 Playing/Result 返回时也要恢复全屏背景与预览（onExit 已 shutdown 音频）
    if (m_selectedGroup >= 0 && m_selectedGroup < static_cast<int>(m_groups.size())) {
        syncSelectionBackground();
        tryPlayPreview();
    }
}

/// 退出选歌状态：停止预览、关闭音频引擎
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

// ══════════════════════════════════════════════════════════════════════════════
//  更新
// ══════════════════════════════════════════════════════════════════════════════

/// 每帧更新：驱动预览音频、同步背景、检测开始游戏（选中谱面后切 Playing）
GameState SongSelectState::update(float dt) {
    m_audio.update(dt);                                      // 预览淡入淡出、循环

    // 用户双击 set 或点 START 后 m_selectedBeatmap 非空 → 配置 PlayingState 并切换
    if (!m_selectedBeatmap.empty()) {
        auto* playing = Kernel::instance().stateManager().getStateAs<PlayingState>(GameState::Playing);
        if (playing) {
            playing->setBeatmapFile(m_selectedBeatmap);    // .mma 或 .osu 路径
            playing->markNeedsReinit();                      // 下次 onEnter 重新 initGameplay
            if (m_selectedGroup >= 0 && m_selectedGroup < static_cast<int>(m_groups.size())) {
                playing->setBackgroundImage(m_groups[m_selectedGroup].imagePath);  // 组共享背景
            }
            std::vector<std::string> activeMods;
            for (const auto& mod : m_mods) {                 // 收集已激活且已实装的 mod
                if (mod.active && mod.implemented) {
                    activeMods.push_back(mod.id);          // 如 nofail、autoplay
                }
            }
            playing->setMods(activeMods);
        }
        return GameState::Playing;                           // 请求状态机切到游玩
    }

    // 选中组变化时同步全屏背景（RANDOM、点击组头等）
    if (m_selectedGroup >= 0 && m_bgImageGroup != m_selectedGroup) {
        syncSelectionBackground();
    }

    return m_nextState;                                      // BACK 等设置的下一状态
}

// ══════════════════════════════════════════════════════════════════════════════
//  渲染
// ══════════════════════════════════════════════════════════════════════════════

/// 渲染选歌 UI、Mod 弹窗、顶部遮罩与延迟删除
void SongSelectState::render() {
    renderImGuiPanel();

    if (!m_modPopupOpen) {
        ImDrawList* fgDl = ImGui::GetForegroundDrawList();
        drawTopArcMask(fgDl);
        renderTopBeatmapInfo();
    }

    if (m_modPopupOpen) {
        renderModPopup();
    }

    applyPendingDelete();
}

void SongSelectState::drawTopArcMask(ImDrawList* dl) const {
    const float W = m_ly.W;
    const float topInfoH = m_ly.topInfoH;
    const float thinH = topInfoH * 0.20f;

    const float seg1End   = W * 0.25f;
    const float arcWidth  = W * 0.20f;
    const float seg3Start = seg1End + arcWidth;

    constexpr int ARC_N = 32;
    constexpr float PI = 3.14159265f;

    const auto arcY = [&](float t) -> float {
        return thinH + (topInfoH - thinH) * (1.0f + cosf(PI * t)) / 2.0f;
    };

    ImVec2 pts[4 + ARC_N + 2];
    int n = 0;
    pts[n++] = ImVec2(0, 0);
    pts[n++] = ImVec2(W, 0);
    pts[n++] = ImVec2(W, thinH);
    pts[n++] = ImVec2(seg3Start, thinH);
    for (int i = ARC_N - 1; i >= 0; --i) {
        const float t = static_cast<float>(i) / ARC_N;
        pts[n++] = ImVec2(seg1End + arcWidth * t, arcY(t));
    }
    pts[n++] = ImVec2(0, topInfoH);
    dl->AddConvexPolyFilled(pts, n, IM_COL32(13, 13, 23, 255));

    ImVec2 edgePts[2 + ARC_N + 2];
    int en = 0;
    edgePts[en++] = ImVec2(0, topInfoH);
    edgePts[en++] = ImVec2(seg1End, topInfoH);
    for (int i = 1; i <= ARC_N; ++i) {
        const float t = static_cast<float>(i) / ARC_N;
        edgePts[en++] = ImVec2(seg1End + arcWidth * t, arcY(t));
    }
    edgePts[en++] = ImVec2(W, thinH);
    dl->AddPolyline(edgePts, en, IM_COL32(255, 255, 255, 200), false, m_ly.scale * 2.0f);

    const float bOff = m_ly.scale * 5.0f;
    ImVec2 bluePts[2 + ARC_N + 2];
    int bn = 0;
    bluePts[bn++] = ImVec2(0, topInfoH - bOff);
    bluePts[bn++] = ImVec2(seg1End, topInfoH - bOff);
    for (int i = 1; i <= ARC_N; ++i) {
        const float t = static_cast<float>(i) / ARC_N;
        bluePts[bn++] = ImVec2(seg1End + arcWidth * t, arcY(t) - bOff);
    }
    bluePts[bn++] = ImVec2(W, thinH - bOff);
    dl->AddPolyline(bluePts, bn, IM_COL32(0, 150, 255, 180), false, m_ly.scale * 1.5f);
}

void SongSelectState::renderTopBeatmapInfo() const {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    if (!font) {
        return;
    }

    const float padX = m_ly.leftWidth * 0.05f;
    const float maxW = m_ly.leftWidth - padX * 2.0f;
    float x = padX;
    float y = m_ly.topInfoH * 0.08f;

    const float titleSize = 28.0f * m_ly.scale;
    const float bodySize = 16.0f * m_ly.scale;
    const float dimSize = 14.0f * m_ly.scale;
    const float lineGap = 5.0f * m_ly.scale;

    const auto drawWrapped = [&](const char* text, float size, ImU32 col) {
        const ImVec2 sz = font->CalcTextSizeA(size, maxW, 0.0f, text);
        dl->AddText(font, size, ImVec2(x, y), col, text, nullptr, maxW);
        y += sz.y + lineGap;
    };

    const BeatmapEntry* sel = getSelectedSet();
    if (!sel) {
        drawWrapped("Select a beatmap", titleSize * 0.75f, IM_COL32(102, 102, 128, 153));
        return;
    }

    const ImU32 cyan = IM_COL32(
        static_cast<int>(ui::Theme::CYAN_R * 255),
        static_cast<int>(ui::Theme::CYAN_G * 255),
        static_cast<int>(ui::Theme::CYAN_B * 255), 255);
    const ImU32 pink = IM_COL32(
        static_cast<int>(ui::Theme::PINK_R * 255),
        static_cast<int>(ui::Theme::PINK_G * 255),
        static_cast<int>(ui::Theme::PINK_B * 255), 230);
    const ImU32 bodyCol = IM_COL32(191, 191, 209, 255);
    const ImU32 dimCol = IM_COL32(153, 153, 179, 255);
    const ImU32 whiteCol = IM_COL32(242, 242, 250, 255);
    const ImU32 goldCol = IM_COL32(255, 200, 50, 255);

    drawWrapped(sel->title.c_str(), titleSize, cyan);

    char line[256];
    std::snprintf(line, sizeof(line), "Artist: %s", sel->artist.c_str());
    drawWrapped(line, bodySize, bodyCol);
    std::snprintf(line, sizeof(line), "Creator: %s", sel->creator.c_str());
    drawWrapped(line, bodySize, bodyCol);

    const int mins = static_cast<int>(sel->duration) / 60;
    const int secs = static_cast<int>(sel->duration) % 60;
    std::snprintf(line, sizeof(line), "Length: %d:%02d   Notes: %d", mins, secs, sel->noteCount);
    drawWrapped(line, dimSize, dimCol);

    std::snprintf(line, sizeof(line), "AR %.1f   OD %.1f   HP %.1f", sel->ar, sel->od, sel->hp);
    dl->AddText(font, bodySize, ImVec2(x, y), whiteCol, line);
    y += bodySize + lineGap;

    std::snprintf(line, sizeof(line), "[%s]", sel->version.c_str());
    dl->AddText(font, bodySize, ImVec2(x, y), pink, line);
    const ImVec2 verSize = font->CalcTextSizeA(bodySize, maxW, 0.0f, line);
    std::snprintf(line, sizeof(line), "%.1f", sel->difficulty);
    dl->AddText(font, bodySize, ImVec2(x + verSize.x + 10.0f * m_ly.scale, y), goldCol, line);
}

// ══════════════════════════════════════════════════════════════════════════════
//  铺面扫描
// ══════════════════════════════════════════════════════════════════════════════

// ══════════════════════════════════════════════════════════════════════════════
//  铺面扫描与增量注册
// ══════════════════════════════════════════════════════════════════════════════

std::optional<SongSelectState::BeatmapEntry> SongSelectState::parseBeatmapEntry(
    const std::string& filePath) {
    try {
        auto readResult = platform::FileSystem::readFile(filePath);
        if (!readResult.ok()) {
            return std::nullopt;
        }

        const std::string& content = readResult.value();
        auto parser = beatmap::createParserForFile(filePath);
        beatmap::BeatmapBuilder builder;
        auto parseResult = parser->parse(content, builder);
        if (!parseResult.ok()) {
            return std::nullopt;
        }

        auto buildResult = builder.build();
        if (!buildResult.ok()) {
            return std::nullopt;
        }

        const auto& beatmap = buildResult.value();
        const std::filesystem::path path(filePath);

        BeatmapEntry e;
        e.filePath = std::filesystem::absolute(path).string();
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
        if (!beatmap.meta.audioFile.empty()) {
            auto audioPath = path.parent_path() / beatmap.meta.audioFile;
            e.audioFilePath = std::filesystem::absolute(audioPath).string();
        }

        const auto bgDir = path.parent_path();
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

        return e;
    } catch (const std::exception& ex) {
        MM_LOG_WARN("SongSelect", "Failed to parse beatmap %s: %s", filePath.c_str(), ex.what());
        return std::nullopt;
    }
}

void SongSelectState::mergeBeatmapEntry(BeatmapEntry entry) {
    for (const auto& group : m_groups) {
        for (const auto& set : group.sets) {
            if (set.filePath == entry.filePath) {
                return;
            }
        }
    }

    const auto sortSets = [](std::vector<BeatmapEntry>& sets) {
        std::sort(sets.begin(), sets.end(),
                  [](const BeatmapEntry& a, const BeatmapEntry& b) {
                      return a.difficulty < b.difficulty;
                  });
    };

    for (auto& group : m_groups) {
        if (group.title == entry.title && group.artist == entry.artist) {
            group.sets.push_back(std::move(entry));
            sortSets(group.sets);
            return;
        }
    }

    BeatmapGroup group;
    group.title = entry.title;
    group.artist = entry.artist;
    group.imagePath = entry.imagePath;
    group.sets.push_back(std::move(entry));

    const auto groupLess = [](const BeatmapGroup& a, const BeatmapGroup& b) {
        if (a.title != b.title) {
            return a.title < b.title;
        }
        return a.artist < b.artist;
    };
    auto insertIt = std::lower_bound(m_groups.begin(), m_groups.end(), group, groupLess);
    m_groups.insert(insertIt, std::move(group));
}

void SongSelectState::registerImportedMma(const std::string& mmaPath) {
    auto entry = parseBeatmapEntry(mmaPath);
    if (!entry) {
        MM_LOG_WARN("SongSelect", "registerImportedMma: failed to parse %s", mmaPath.c_str());
        return;
    }

    const std::string imagePath = entry->imagePath;
    mergeBeatmapEntry(std::move(*entry));
    m_scanDone = true;

    if (!imagePath.empty()) {
        renderer::TextureCache::instance().requestLoad(imagePath, false);
    }

    MM_LOG_INFO("SongSelect", "Registered imported beatmap: %s (%zu groups)",
                mmaPath.c_str(), m_groups.size());
}

/// 扫描铺面目录，解析 .mma/.osu 并构建分组列表（含导入的 osu 谱面，可后台线程调用）
void SongSelectState::scanBeatmaps() {
    MM_LOG_INFO("SongSelect", "Scanning beatmaps in: " + m_beatmapDir);

    m_groups.clear();                                        // 清空旧列表
    std::vector<BeatmapEntry> entries;                       // 扁平条目，后续再分组

    try {
        std::filesystem::path beatmapDir(m_beatmapDir);       // 默认 assets/beatmaps
        if (!std::filesystem::exists(beatmapDir)) {
            MM_LOG_WARN("SongSelect", "Beatmap directory does not exist: " + m_beatmapDir);
            m_scanDone = true;
            return;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(beatmapDir)) {
            if (!entry.is_regular_file()) continue;          // 跳过目录

            std::string ext = entry.path().extension().string();
            for (auto& c : ext) c = static_cast<char>(tolower(c));  // 扩展名小写

            if (ext != ".mma" && ext != ".osu") continue;     // 仅游戏支持的谱面格式

            std::string filePath = std::filesystem::absolute(entry.path()).string();

            if (auto parsed = parseBeatmapEntry(filePath)) {
                entries.push_back(std::move(*parsed));
            }
        }
    } catch (const std::exception& e) {
        MM_LOG_WARN("SongSelect", "Failed to scan beatmaps: %s", e.what());
    }

    // 目录为空时注入 demo 谱面，保证可进入游玩
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
        e.imagePath = std::filesystem::absolute("assets/textures/menu-bg.jpg").string();
        entries.push_back(e);
    }

    std::sort(entries.begin(), entries.end(),
        [](const BeatmapEntry& a, const BeatmapEntry& b) {
            if (a.title != b.title) return a.title < b.title;
            return a.difficulty < b.difficulty;              // 同歌按星级升序
        });

    // ── 按 title+artist 合并为 BeatmapGroup（一组多难度 set）──
    // 新组创建时 requestLoad 一次背景图，decode 可与后续扫描并行（TextureCache 去重）
    for (auto& e : entries) {
        if (!m_groups.empty() && m_groups.back().title == e.title && m_groups.back().artist == e.artist) {
            m_groups.back().sets.push_back(std::move(e));    // 追加到当前组
        } else {
            BeatmapGroup g;
            g.title = e.title;
            g.artist = e.artist;
            g.imagePath = e.imagePath;
            g.sets.push_back(std::move(e));
            if (!g.imagePath.empty()) {
                renderer::TextureCache::instance().requestLoad(g.imagePath, false);
            }
            m_groups.push_back(std::move(g));                // 新组
        }
    }

    m_scanDone = true;
    MM_LOG_INFO("SongSelect", "Found %d beatmap groups", static_cast<int>(m_groups.size()));
}

// ══════════════════════════════════════════════════════════════════════════════
//  辅助
// ══════════════════════════════════════════════════════════════════════════════

/// 获取当前选中的难度 set 条目
const SongSelectState::BeatmapEntry* SongSelectState::getSelectedSet() const {
    if (m_selectedGroup < 0 || m_selectedGroup >= static_cast<int>(m_groups.size())) return nullptr;
    const auto& group = m_groups[m_selectedGroup];
    if (m_selectedSet < 0 || m_selectedSet >= static_cast<int>(group.sets.size())) return nullptr;
    return &group.sets[m_selectedSet];
}

/// 删除铺面后修正选中组/set 索引并刷新预览与背景
void SongSelectState::fixSelectionAfterDelete() {
    m_selectedBeatmap.clear();

    if (m_groups.empty()) {
        m_selectedGroup = -1;
        m_selectedSet = -1;
        syncSelectionBackground();
        return;
    }

    if (m_selectedGroup < 0) {
        m_selectedGroup = 0;
    }
    if (m_selectedGroup >= static_cast<int>(m_groups.size())) {
        m_selectedGroup = static_cast<int>(m_groups.size()) - 1;
    }

    auto& group = m_groups[m_selectedGroup];
    if (group.sets.empty()) {
        m_groups.erase(m_groups.begin() + m_selectedGroup);
        fixSelectionAfterDelete();
        return;
    }

    if (m_selectedSet < 0) {
        m_selectedSet = 0;
    }
    if (m_selectedSet >= static_cast<int>(group.sets.size())) {
        m_selectedSet = static_cast<int>(group.sets.size()) - 1;
    }

    syncSelectionBackground();
    tryPlayPreview();
}

/// 在 render 末尾执行待处理的删除操作（支持同帧多条，按索引降序避免错位）
void SongSelectState::applyPendingDelete() {
    if (m_pendingDeletes.empty()) {
        return;
    }

    auto actions = std::move(m_pendingDeletes);
    m_pendingDeletes.clear();

    std::sort(actions.begin(), actions.end(),
              [](const PendingDeleteAction& a, const PendingDeleteAction& b) {
                  if (a.groupIndex != b.groupIndex) {
                      return a.groupIndex > b.groupIndex;
                  }
                  return a.setIndex > b.setIndex;
              });

    for (const auto& action : actions) {
        if (action.setIndex < 0) {
            deleteBeatmapGroup(action.groupIndex);
        } else {
            deleteBeatmapSet(action.groupIndex, action.setIndex);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  删除铺面
// ══════════════════════════════════════════════════════════════════════════════

/// 删除整组铺面（磁盘文件 + 内存列表）
void SongSelectState::deleteBeatmapGroup(int groupIndex) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(m_groups.size())) {
        return;
    }

    const auto& group = m_groups[groupIndex];
    m_audio.stop();
    m_lastPreviewAudioPath.clear();

    std::vector<std::string> imagePaths;
    std::vector<std::string> sourceHashes;
    if (!group.imagePath.empty()) {
        imagePaths.push_back(group.imagePath);
    }
    for (const auto& set : group.sets) {
        const std::string hash = beatmap::MmaSerializer::readSourceHash(set.filePath);
        if (!hash.empty()) {
            sourceHashes.push_back(hash);
        }
        if (!set.imagePath.empty() &&
            std::find(imagePaths.begin(), imagePaths.end(), set.imagePath) == imagePaths.end()) {
            imagePaths.push_back(set.imagePath);
        }
    }

    if (group.sets.empty()) {
        m_groups.erase(m_groups.begin() + groupIndex);
        releaseDeletedBeatmapAssets(imagePaths, sourceHashes);
        fixSelectionAfterDelete();
        return;
    }

    std::filesystem::path dir = std::filesystem::path(group.sets[0].filePath).parent_path();
    bool sameDir = true;
    for (const auto& set : group.sets) {
        if (std::filesystem::path(set.filePath).parent_path() != dir) {
            sameDir = false;
            break;
        }
    }

    std::error_code ec;
    if (sameDir) {
        std::filesystem::remove_all(dir, ec);
        if (ec == std::errc::no_such_file_or_directory) {
            ec.clear();
        }
        if (ec) {
            MM_LOG_WARN("SongSelect", "Failed to delete beatmap folder: %s (%s)",
                        dir.string().c_str(), ec.message().c_str());
        }
    } else {
        for (const auto& set : group.sets) {
            std::filesystem::remove(set.filePath, ec);
            if (ec == std::errc::no_such_file_or_directory) {
                ec.clear();
                continue;
            }
            if (ec) {
                MM_LOG_WARN("SongSelect", "Failed to delete beatmap file: %s (%s)",
                            set.filePath.c_str(), ec.message().c_str());
                break;
            }
        }
    }

    MM_LOG_INFO("SongSelect", "Deleted beatmap group: %s - %s",
                group.artist.c_str(), group.title.c_str());
    m_groups.erase(m_groups.begin() + groupIndex);
    releaseDeletedBeatmapAssets(imagePaths, sourceHashes);
    fixSelectionAfterDelete();
    m_audio.playSfx(audio::SfxType::MenuClick);
}

/// 删除组内单个难度 set
void SongSelectState::deleteBeatmapSet(int groupIndex, int setIndex) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(m_groups.size())) {
        return;
    }

    auto& group = m_groups[groupIndex];
    if (setIndex < 0 || setIndex >= static_cast<int>(group.sets.size())) {
        return;
    }

    const std::string path = group.sets[setIndex].filePath;
    m_audio.stop();
    m_lastPreviewAudioPath.clear();

    std::vector<std::string> imagePaths;
    std::vector<std::string> sourceHashes;
    const std::string hash = beatmap::MmaSerializer::readSourceHash(path);
    if (!hash.empty()) {
        sourceHashes.push_back(hash);
    }
    const bool removingLastSet = group.sets.size() == 1;
    if (removingLastSet && !group.imagePath.empty()) {
        imagePaths.push_back(group.imagePath);
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        MM_LOG_WARN("SongSelect", "Failed to delete beatmap file: %s (%s)",
                    path.c_str(), ec.message().c_str());
    }

    MM_LOG_INFO("SongSelect", "Deleted beatmap set: %s", path.c_str());
    group.sets.erase(group.sets.begin() + setIndex);
    if (group.sets.empty()) {
        m_groups.erase(m_groups.begin() + groupIndex);
    }

    releaseDeletedBeatmapAssets(imagePaths, sourceHashes);
    fixSelectionAfterDelete();
    m_audio.playSfx(audio::SfxType::MenuClick);
}

// ══════════════════════════════════════════════════════════════════════════════
//  纹理 / 头像
// ══════════════════════════════════════════════════════════════════════════════

/// 加载用户头像纹理
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

/// 按需加载指定分组的背景图到全局 TextureCache
void SongSelectState::tryLoadGroupImage(int groupIndex) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(m_groups.size())) return;

    const auto& group = m_groups[groupIndex];
    if (group.imagePath.empty()) return;

    renderer::TextureCache::instance().requestLoad(group.imagePath, false);
}

void SongSelectState::syncSelectionBackground() {
    auto& renderer = Kernel::instance().renderer();

    if (m_selectedGroup < 0 || m_selectedGroup >= static_cast<int>(m_groups.size())) {
        renderer.setBackgroundPath("");
        m_bgImageGroup = -1;
        return;
    }

    const auto& group = m_groups[m_selectedGroup];
    if (!group.imagePath.empty()) {
        tryLoadGroupImage(m_selectedGroup);
    }
    m_bgImageGroup = m_selectedGroup;

    if (group.imagePath.find("menu-bg.jpg") == std::string::npos) {
        renderer.setBackgroundPath(group.imagePath);
    } else {
        renderer.setBackgroundPath("");
    }
}

void SongSelectState::releaseDeletedBeatmapAssets(
    const std::vector<std::string>& imagePaths,
    const std::vector<std::string>& sourceHashes) {
    auto& cache = renderer::TextureCache::instance();
    for (const auto& path : imagePaths) {
        if (path.empty() || path.find("menu-bg.jpg") != std::string::npos) {
            continue;
        }
        cache.unload(path);
    }

    auto* mainMenu = Kernel::instance().stateManager().getStateAs<MainMenuState>(GameState::MainMenu);
    if (mainMenu) {
        for (const auto& hash : sourceHashes) {
            mainMenu->importedHashes().erase(hash);          // 删除后允许重新 import 同一 osu
        }
    }

    unloadUnusedImages();
}

/// 卸载当前未选中分组的背景纹理（节省显存）
void SongSelectState::unloadUnusedImages() {
    auto paths = getGroupImagePaths();
    renderer::TextureCache::instance().unloadDistant(paths, m_selectedGroup, 5);
}

// ══════════════════════════════════════════════════════════════════════════════
//  主面板渲染
// ══════════════════════════════════════════════════════════════════════════════

// ══════════════════════════════════════════════════════════════════════════════
//  UI 渲染
// ══════════════════════════════════════════════════════════════════════════════

/// 渲染选歌主面板（左右分割 + 底部操作栏）
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

    if (m_modPopupOpen) {
        flags |= ImGuiWindowFlags_NoInputs;
    }

    ImGui::Begin("##SongSelect", nullptr, flags);

    // ── 全屏背景图已由 Renderer::renderBackground() 绘制，此处不再重复渲染 ──
    // 顶部弧形遮罩与铺面详情在 render() 中于 ForegroundDrawList 统一绘制

    renderLeftPanel(m_ly.leftWidth, H);
    renderRightPanel(m_ly.leftWidth, m_ly.rightWidth, H);

    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
//  左侧面板
// ══════════════════════════════════════════════════════════════════════════════

/// 渲染左侧面板：头像、歌曲信息、排行榜、Mod 按钮
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
    if (m_modPopupOpen) {
        childFlags |= ImGuiWindowFlags_NoInputs;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m_ly.leftWidth * 0.05f, m_ly.H * 0.015f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        ImVec4(ui::Theme::BG_R, ui::Theme::BG_G, ui::Theme::BG_B, 0.0f));

    ImGui::BeginChild("##LeftPanel", ImVec2(panelWidth, panelHeight), false, childFlags);

    // 顶部留给 ForegroundDrawList 铺面详情 + 弧形遮罩
    ImGui::Dummy(ImVec2(0, m_ly.topInfoH));

    // ── 排行榜区域 ──
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
        ImGuiWindowFlags lbFlags = ImGuiWindowFlags_None;
        if (m_modPopupOpen) {
            lbFlags |= ImGuiWindowFlags_NoInputs;
        }
        ImGui::BeginChild("##Leaderboard", ImVec2(panelWidth - panelWidth * 0.08f, lbHeight), false, lbFlags);

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
        m_audio.playSfx(audio::SfxType::MenuHit);
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
            m_audio.playSfx(audio::SfxType::MenuClick);
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
        m_audio.playSfx(audio::SfxType::MenuHit);
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

/// 渲染右侧铺面列表（分组头 + 难度条目 + 滚动）
void SongSelectState::renderRightPanel(float panelX, float panelWidth, float panelHeight) {
    using namespace ui;

    ImGui::SetCursorPos(ImVec2(panelX, 0));

    ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (m_modPopupOpen) {
        childFlags |= ImGuiWindowFlags_NoInputs;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m_ly.rightPadding, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        ImVec4(ui::Theme::BG_R, ui::Theme::BG_G, ui::Theme::BG_B, 0.0f));

    ImGui::BeginChild("##RightPanel", ImVec2(panelWidth, panelHeight), false, childFlags);

    // ── 可滚动铺面列表（右侧缩进 + 右侧无边距到达屏幕右缘）──
    float listWidth = m_ly.rightWidth - m_ly.listIndent - m_ly.rightPadding;
    float listHeight = panelHeight - m_ly.bottomBarH;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + m_ly.listIndent);
    ImGuiWindowFlags listFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (m_modPopupOpen) {
        listFlags |= ImGuiWindowFlags_NoInputs;
    }
    ImGui::BeginChild("##BeatmapList", ImVec2(listWidth, listHeight), false, listFlags);

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

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            m_selectedGroup = g;
            m_selectedSet = 0;  // 自动选中第一个 set
            m_scrollToSelected = true;
            tryLoadGroupImage(g);
            tryPlayPreview();   // 组头点击后触发预览
            m_audio.playSfx(audio::SfxType::MenuClick);
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                m_pendingDeletes.push_back(PendingDeleteAction{g, -1});
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
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
            (isSelectedGroup ? 20.0f : 17.0f) * m_ly.scale,
            ImVec2(textX, textY),
            isSelectedGroup ? IM_COL32(0, 255, 245, 255) : IM_COL32(240, 240, 250, 255),
            group.title.c_str());

        float subY = textY + m_ly.groupHeaderH * (isSelectedGroup ? 0.33f : 0.28f);

        // 艺术家 + set 数量
        char subText[128];
        snprintf(subText, sizeof(subText), "%s  |  %d sets", group.artist.c_str(),
                 static_cast<int>(group.sets.size()));
        dl->AddText(ImGui::GetFont(), 14.0f * m_ly.scale,
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
            dl->AddText(ImGui::GetFont(), 14.0f * m_ly.scale,
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

                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    m_selectedSet = s;
                    tryPlayPreview();   // set 单击后触发预览
                    m_audio.playSfx(audio::SfxType::MenuClick);
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    m_selectedSet = s;
                    m_selectedBeatmap = set.filePath;
                    m_audio.playSfx(audio::SfxType::MenuHit);
                    tryPlayPreview();   // 双击时也触发（防重复逻辑在内部）
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete")) {
                        m_pendingDeletes.push_back(PendingDeleteAction{g, s});
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // set 文字
                float setTextX = setDrawPos.x + m_ly.setItemH * 0.3f;
                float setTextY = setDrawPos.y + (m_ly.setItemH - m_ly.setItemH * 0.35f) * 0.5f;

                // 难度名称（AddText 字号按布局缩放；atlas 已按分辨率烘焙）
                dl->AddText(ImGui::GetFont(), 15.0f * m_ly.scale,
                    ImVec2(setTextX, setTextY),
                    isSelectedSet ? IM_COL32(0, 255, 245, 255) : IM_COL32(200, 200, 215, 255),
                    set.version.c_str());

                // 星级（右侧）
                char starText[32];
                snprintf(starText, sizeof(starText), "%.1f", set.difficulty);
                float starX = setDrawPos.x + setWidth - setWidth * 0.12f;
                dl->AddText(ImGui::GetFont(), 14.0f * m_ly.scale,
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
        // 字号已由 UIManager 按分辨率烘焙进 atlas，勿再乘 m_ly.scale
        ImGui::SetWindowFontScale(1.2f);
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
            m_audio.playSfx(audio::SfxType::MenuHit);
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

/// 渲染 Mod 选择弹窗（NoFail / Autoplay 等）
void SongSelectState::renderModPopup() {
    using namespace ui;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // 全屏 modal 遮罩：接收输入（不用 NoInputs），点击弹窗外区域关闭
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);

        ImGuiWindowFlags backdropFlags = ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoNavFocus |
                                         ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.5f));
        ImGui::Begin("##ModBackdrop", nullptr, backdropFlags);

        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##modBackdropBtn", displaySize);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            m_modPopupOpen = false;
            m_audio.playSfx(audio::SfxType::MenuHit);
        }

        ImGui::End();
        ImGui::PopStyleColor();
    }

    // 弹窗（绘制在 backdrop 之上，内部控件正常可点）
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
    // 字号已由 UIManager 按分辨率烘焙进 atlas，勿再乘 m_ly.scale
    ImGui::SetWindowFontScale(1.6f);
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
                m_audio.playSfx(audio::SfxType::MenuHit);
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
        m_audio.playSfx(audio::SfxType::MenuHit);
    }
    ImGui::PopStyleColor(4);

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// ══════════════════════════════════════════════════════════════════════════════
//  预览音频
// ══════════════════════════════════════════════════════════════════════════════

/// 尝试播放当前选中 set 的预览音频（同路径不重启）
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

/// 扫描铺面（背景图在 scanBeatmaps 按组 requestLoad；保留 API 供外部显式重扫）
void SongSelectState::scanAndPreload() {
    if (!m_scanDone) {
        scanBeatmaps();
    }

    MM_LOG_INFO("SongSelect", "scanAndPreload complete: %zu groups, %zu image paths",
                m_groups.size(), getGroupImagePaths().size());
}

/// 获取所有分组的背景图路径列表（供 BootState 预加载）
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
