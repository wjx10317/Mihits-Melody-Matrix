#include "main_menu_state.h"
#include "core/kernel.h"
#include "core/states/song_select_state.h"
#include "renderer/texture_cache.h"
#include "ui/theme.h"
#include "util/logger.h"
#include "util/hash.h"
#include "platform/file_dialog.h"
#include "platform/file_system.h"
#include "platform/config.h"
#include "platform/zip_extract.h"
#include "beatmap/beatmap_parser.h"
#include "beatmap/beatmap_builder.h"
#include "beatmap/mma_serializer.h"
#include "beatmap/note.h"

#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace melody_matrix::core {

// ──────────────────────────────────────────────────────
//  辅助函数
// ──────────────────────────────────────────────────────

/// 替换文件名中不允许的字符
static std::string sanitizeFilename(const std::string& name) {
    std::string result = name;
    for (auto& c : result) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    while (!result.empty() && result.front() == ' ') result.erase(result.begin());
    while (!result.empty() && result.back() == ' ') result.pop_back();
    if (result.empty()) result = "Unknown";
    if (result.size() > 200) result = result.substr(0, 200);
    return result;
}

// ──────────────────────────────────────────────────────
//  生命周期
// ──────────────────────────────────────────────────────

void MainMenuState::loadBackgroundTexture() {
    if (!m_bgTexture.valid()) {
        const char* paths[] = {
            "assets/textures/menu-bg.jpg",
            "../assets/textures/menu-bg.jpg",
            "../../assets/textures/menu-bg.jpg"
        };
        for (const char* p : paths) {
            if (m_bgTexture.loadFromFile(p, false)) {
                MM_LOG_INFO("MainMenu", "Background loaded from: %s", p);
                return;
            }
        }
        MM_LOG_WARN("MainMenu", "Failed to load background texture from all paths");
    }
}

void MainMenuState::onEnter() {
    MM_LOG_INFO("MainMenu", "Entering Main Menu");
    m_nextState = GameState::Count;
    m_settingsOpen = false;
    m_settingsSlideProgress = 0.0f;
    m_pendingResolutionW = 0;
    m_pendingResolutionH = 0;
    m_hasPendingFullscreen = false;
    m_importInProgress = false;
    m_importMessage.clear();
    m_importMessageTimer = 0.0f;

    loadBackgroundTexture();

    // 加载当前分辨率设置
    int curW = 0, curH = 0;
    Kernel::instance().getCurrentResolution(curW, curH);
    m_fullscreen = Kernel::instance().isFullscreen();

    const auto& resos = Kernel::supportedResolutions();
    m_selectedResolutionIndex = 1;
    for (int i = 0; i < static_cast<int>(resos.size()); ++i) {
        if (resos[i].width == curW && resos[i].height == curH) {
            m_selectedResolutionIndex = i;
            break;
        }
    }

    // 构建已导入哈希集合：扫描 assets/beatmaps/ 下所有 .mma 文件
    if (m_importedHashes.empty()) {
        try {
            std::filesystem::path beatmapDir("assets/beatmaps");
            if (std::filesystem::exists(beatmapDir)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(beatmapDir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".mma") {
                        std::string hash = beatmap::MmaSerializer::readSourceHash(entry.path().string());
                        if (!hash.empty()) {
                            m_importedHashes.insert(hash);
                        }
                    }
                }
            }
            MM_LOG_INFO("MainMenu", "Loaded %zu imported beatmap hashes", m_importedHashes.size());
        } catch (const std::exception& e) {
            MM_LOG_WARN("MainMenu", "Failed to scan imported hashes: %s", e.what());
        }
    }
}

void MainMenuState::onExit() {
    MM_LOG_INFO("MainMenu", "Exiting Main Menu");
}

GameState MainMenuState::update(float dt) {
    // Apply deferred resolution/fullscreen changes
    if (m_pendingResolutionW > 0 && m_pendingResolutionH > 0) {
        Kernel::instance().setResolution(m_pendingResolutionW, m_pendingResolutionH);
        m_pendingResolutionW = 0;
        m_pendingResolutionH = 0;
    }
    if (m_hasPendingFullscreen) {
        Kernel::instance().setFullscreen(m_pendingFullscreen);
        m_fullscreen = m_pendingFullscreen;
        m_hasPendingFullscreen = false;
    }

    // Animate settings sidebar slide
    float slideSpeed = 6.0f;
    if (m_settingsOpen) {
        m_settingsSlideProgress = std::min(m_settingsSlideProgress + dt * slideSpeed, 1.0f);
    } else {
        m_settingsSlideProgress = std::max(m_settingsSlideProgress - dt * slideSpeed, 0.0f);
    }

    // 导入消息计时器
    if (m_importMessageTimer > 0.0f) {
        m_importMessageTimer -= dt;
        if (m_importMessageTimer <= 0.0f) {
            m_importMessage.clear();
        }
    }

    return m_nextState;
}

// ──────────────────────────────────────────────────────
//  导入功能
// ──────────────────────────────────────────────────────

void MainMenuState::importOszFile(const std::string& oszPath) {
    auto result = validateAndImportOsz(oszPath);
    if (result.ok()) {
        m_importSuccess = true;
        m_importMessageTimer = 3.0f;
        // result 不携带数据，消息由 validateAndImportOsz 内部设置
    } else {
        // ALREADY_IMPORTED 是静默跳过
        if (result.error().code == static_cast<int32_t>(util::ErrorCode::ERROR_BEATMAP_ALREADY_IMPORTED)) {
            m_importMessage.clear();
            return;
        }
        m_importSuccess = false;
        m_importMessage = "Import failed: " + result.error().message;
        m_importMessageTimer = 5.0f;
    }
}

util::Result<void> MainMenuState::validateAndImportOsz(const std::string& oszPath) {
    using namespace util;

    // ── V1: 文件扩展名 ──
    std::string ext;
    {
        auto dotPos = oszPath.rfind('.');
        if (dotPos == std::string::npos) {
            return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_IO),
                                "File has no extension: " + oszPath);
        }
        ext = oszPath.substr(dotPos + 1);
        for (auto& c : ext) c = static_cast<char>(tolower(c));
        if (ext != "osz") {
            return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_IO),
                                "Not an .osz file: " + oszPath);
        }
    }

    // ── V2: 文件可读（基本存在性检查）──
    if (!std::filesystem::exists(oszPath)) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_FILE_NOT_FOUND),
                            "File not found: " + oszPath);
    }

    // ── V2.5: 解压 .osz 到临时目录 ──
    std::string tempDir = platform::ZipExtract::createTempDir();
    if (tempDir.empty()) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_IO),
                            "Cannot create temp directory for extraction");
    }

    // RAII 清理：函数退出时删除临时目录
    struct TempDirGuard {
        std::string path;
        ~TempDirGuard() { platform::ZipExtract::removeDir(path); }
    } tempDirGuard{tempDir};

    if (!platform::ZipExtract::extract(oszPath, tempDir)) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_INVALID_ARCHIVE),
                            "Failed to extract .osz file");
    }

    // ── V2.6: 查找 .osu 文件 ──
    auto osuFiles = platform::ZipExtract::findOsuFiles(tempDir);
    if (osuFiles.empty()) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_NO_OSU_IN_ARCHIVE),
                            "No .osu files found in .osz archive");
    }

    MM_LOG_INFO("Import", "Found %zu .osu files in archive", osuFiles.size());

    // ── 逐个处理 .osu 文件 ──
    int importedCount = 0;
    int skippedCount = 0;
    std::string firstTitle;
    std::string lastError;

    for (const auto& osuPath : osuFiles) {
        auto singleResult = importSingleOsu(osuPath, tempDir);
        if (singleResult.ok()) {
            ++importedCount;
        } else if (singleResult.error().code ==
                   static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_ALREADY_IMPORTED)) {
            ++skippedCount;
        } else {
            // 记录错误但继续处理其他 .osu
            lastError = singleResult.error().message;
            MM_LOG_WARN("Import", "Failed to import %s: %s",
                        osuPath.c_str(), lastError.c_str());
        }
    }

    // ── 汇总结果消息 ──
    if (importedCount > 0) {
        m_importMessage = "Imported " + std::to_string(importedCount) + " beatmap(s)";
        if (skippedCount > 0) {
            m_importMessage += " (" + std::to_string(skippedCount) + " already exists)";
        }

        // 通知 SongSelectState 重新扫描
        auto* songSelect = Kernel::instance().stateManager().getStateAs<SongSelectState>(GameState::SongSelect);
        if (songSelect) {
            songSelect->markNeedsRescan();
        }

        // 即时缓存新导入的背景图到全局纹理缓存
        {
            namespace fs = std::filesystem;
            fs::path beatmapsDir = fs::absolute("assets/beatmaps");
            if (fs::exists(beatmapsDir) && fs::is_directory(beatmapsDir)) {
                for (const auto& entry : fs::directory_iterator(beatmapsDir)) {
                    if (!entry.is_directory()) continue;
                    // 查找目录中的 background.* 文件
                    for (const auto& f : fs::directory_iterator(entry.path())) {
                        std::string fileExt = f.path().extension().string();
                        for (char& c : fileExt) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                        if (fileExt == ".png" || fileExt == ".jpg" || fileExt == ".jpeg" || fileExt == ".webp") {
                            std::string name = f.path().stem().string();
                            if (name == "background") {
                                renderer::TextureCache::instance().load(fs::absolute(f.path()).string(), false);
                            }
                        }
                    }
                }
            }
        }
    } else if (skippedCount > 0) {
        // 全部已导入 = 静默跳过
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_ALREADY_IMPORTED), "");
    } else {
        // 全部失败
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_PARSE),
                            lastError.empty() ? "All .osu files failed to import" : lastError);
    }

    return success();
}

util::Result<void> MainMenuState::importSingleOsu(const std::string& osuPath, const std::string& extractRoot) {
    using namespace util;

    // ── V3: 读取并检查格式头 ──
    std::string content;
    {
        auto readResult = platform::FileSystem::readFile(osuPath);
        if (!readResult.ok()) {
            return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_FILE_NOT_FOUND),
                                "Cannot read .osu: " + osuPath);
        }
        content = readResult.value();
    }

    {
        auto firstNewline = content.find('\n');
        std::string firstLine = content.substr(0, firstNewline);
        if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();
        if (firstLine.find("osu file format v") == std::string::npos) {
            return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_INVALID_HEADER),
                                "Invalid .osu header: " + firstLine);
        }
    }

    // ── V4: Mode 检查（仅 standard）──
    {
        auto generalPos = content.find("[General]");
        if (generalPos != std::string::npos) {
            auto nextSection = content.find('[', generalPos + 9);
            std::string generalSection = content.substr(generalPos,
                nextSection == std::string::npos ? std::string::npos : nextSection - generalPos);
            auto modePos = generalSection.find("Mode:");
            if (modePos == std::string::npos) {
                modePos = generalSection.find("Mode=");
            }
            if (modePos != std::string::npos) {
                auto lineEnd = generalSection.find('\n', modePos);
                std::string modeLine = generalSection.substr(modePos, lineEnd - modePos);
                auto colonPos = modeLine.find(':');
                auto equalsPos = modeLine.find('=');
                auto sepPos = (colonPos != std::string::npos) ? colonPos : equalsPos;
                if (sepPos != std::string::npos) {
                    std::string modeVal = modeLine.substr(sepPos + 1);
                    while (!modeVal.empty() && (modeVal.front() == ' ' || modeVal.front() == '\t'))
                        modeVal.erase(modeVal.begin());
                    while (!modeVal.empty() && (modeVal.back() == ' ' || modeVal.back() == '\t' ||
                           modeVal.back() == '\r'))
                        modeVal.pop_back();
                    if (modeVal != "0") {
                        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_NOT_STANDARD),
                                            "Only osu!standard (Mode=0) is supported, got Mode=" + modeVal);
                    }
                }
            }
            // Mode 不存在默认为 0 (standard)
        }
    }

    // ── SHA256 去重 ──
    std::string fileHash = util::sha256(content.data(), content.size());
    if (m_importedHashes.count(fileHash) > 0) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_ALREADY_IMPORTED), "");
    }

    // ── V5: 解析 .osu ──
    auto parser = beatmap::createParserForFile(osuPath);
    beatmap::BeatmapBuilder builder;
    auto parseResult = parser->parse(content, builder);
    if (!parseResult.ok()) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_PARSE),
                            "Parse error: " + parseResult.error().message);
    }

    // ── V6: 有音符 ──
    auto buildResult = builder.build();
    if (!buildResult.ok()) {
        return Result<void>(buildResult.error().code,
                            "Build error: " + buildResult.error().message);
    }

    auto& beatmap = buildResult.value();
    if (beatmap.notes.empty()) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_EMPTY_NOTES),
                            "Beatmap has no notes");
    }

    // ── V7: 有音频引用 ──
    if (beatmap.meta.audioFile.empty()) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_NO_AUDIO_REF),
                            "Beatmap has no audio file reference");
    }

    // ── V8: Title 非空 ──
    if (beatmap.meta.title.empty()) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_NO_TITLE),
                            "Beatmap title is empty");
    }

    // ── V11: Version 空则默认填充 ──
    if (beatmap.meta.version.empty()) {
        beatmap.meta.version = "Normal";
    }

    // ── 构建目标路径 ──
    std::string safeTitle = sanitizeFilename(beatmap.meta.title);
    std::string safeVersion = sanitizeFilename(beatmap.meta.version);
    std::filesystem::path targetDir = std::filesystem::path("assets/beatmaps") / safeTitle;
    std::filesystem::path targetPath = targetDir / (safeVersion + ".mma");

    // ── 创建目录 ──
    try {
        std::filesystem::create_directories(targetDir);
    } catch (const std::exception& e) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_IO),
                            std::string("Cannot create directory: ") + e.what());
    }

    // ── 写入 .mma 文件 ──
    auto writeResult = beatmap::MmaSerializer::writeToFile(beatmap, targetPath.string(), fileHash);
    if (!writeResult.ok()) {
        return writeResult;
    }

    // ── V9: 复制音频文件（从解压目录）──
    {
        // osuPath 在解压临时目录中，音频文件在同一目录或子目录
        std::filesystem::path osuDir = std::filesystem::path(osuPath).parent_path();
        std::filesystem::path audioSrc = osuDir / beatmap.meta.audioFile;
        std::filesystem::path audioDst = targetDir / beatmap.meta.audioFile;

        // 也搜索解压根目录（有些 .osz 的音频在根目录，.osu 在子目录）
        if (!std::filesystem::exists(audioSrc)) {
            std::filesystem::path rootAudioSrc = std::filesystem::path(extractRoot) / beatmap.meta.audioFile;
            if (std::filesystem::exists(rootAudioSrc)) {
                audioSrc = rootAudioSrc;
            }
        }

        if (audioDst.has_parent_path()) {
            std::filesystem::create_directories(audioDst.parent_path());
        }

        if (std::filesystem::exists(audioSrc)) {
            if (!std::filesystem::exists(audioDst)) {
                std::filesystem::copy_file(audioSrc, audioDst,
                    std::filesystem::copy_options::overwrite_existing);
                MM_LOG_INFO("Import", "Audio copied: %s", beatmap.meta.audioFile.c_str());
            }
        } else {
            MM_LOG_WARN("Import", "Audio not found in archive: %s", beatmap.meta.audioFile.c_str());
        }
    }

    // ── V10: 复制背景图（从解压目录）→ 重命名为 background.扩展名 ──
    {
        auto eventsPos = content.find("[Events]");
        if (eventsPos != std::string::npos) {
            auto nextSection = content.find('[', eventsPos + 8);
            std::string eventsSection = content.substr(eventsPos,
                nextSection == std::string::npos ? std::string::npos : nextSection - eventsPos);

            std::istringstream ess(eventsSection);
            std::string line;
            while (std::getline(ess, line)) {
                if (line.empty() || line[0] == '[' || line[0] == '/' || line[0] == '_') continue;

                auto q1 = line.find('"');
                if (q1 != std::string::npos) {
                    auto q2 = line.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        std::string bgFile = line.substr(q1 + 1, q2 - q1 - 1);
                        if (!bgFile.empty()) {
                            // 在 .osu 同目录和解压根目录搜索
                            std::filesystem::path bgSrc = std::filesystem::path(osuPath).parent_path() / bgFile;
                            if (!std::filesystem::exists(bgSrc)) {
                                bgSrc = std::filesystem::path(extractRoot) / bgFile;
                            }

                            if (std::filesystem::exists(bgSrc)) {
                                // 目标文件名固定为 background + 原始后缀
                                std::string bgExt = std::filesystem::path(bgFile).extension().string();
                                std::filesystem::path bgDst = targetDir / ("background" + bgExt);

                                if (!std::filesystem::exists(bgDst)) {
                                    try {
                                        if (bgDst.has_parent_path()) {
                                            std::filesystem::create_directories(bgDst.parent_path());
                                        }
                                        std::filesystem::copy_file(bgSrc, bgDst,
                                            std::filesystem::copy_options::overwrite_existing);
                                        MM_LOG_INFO("Import", "Background copied: %s -> background%s",
                                                    bgFile.c_str(), bgExt.c_str());
                                    } catch (const std::exception& e) {
                                        MM_LOG_WARN("Import", "Failed to copy background: %s", e.what());
                                    }
                                }
                            } else {
                                MM_LOG_WARN("Import", "Background not found in archive: %s", bgFile.c_str());
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    // ── 注册哈希 ──
    m_importedHashes.insert(fileHash);

    MM_LOG_INFO("Import", "Successfully imported: %s -> %s", osuPath.c_str(), targetPath.string().c_str());
    return success();
}

// ──────────────────────────────────────────────────────
//  渲染
// ──────────────────────────────────────────────────────

void MainMenuState::render() {
    // ── 背景图片 ──
    if (m_bgTexture.valid()) {
        ImDrawList* bgDl = ImGui::GetBackgroundDrawList();
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        bgDl->AddImage(
            (ImTextureID)(intptr_t)m_bgTexture.textureId(),
            ImVec2(0, 0),
            displaySize,
            ImVec2(0, 1), ImVec2(1, 0),
            IM_COL32(255, 255, 255, 255)
        );
    }

    // Render order: MainMenu → Overlay → Sidebar (last = on top)
    renderImGuiPanel();
    if (m_settingsSlideProgress > 0.001f) {
        renderSettingsOverlay();
        renderSettingsSidebar();
    }

    // Close settings when clicking outside the sidebar area
    if (m_settingsOpen && m_settingsSlideProgress > 0.5f &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        float eased = 1.0f - std::pow(1.0f - m_settingsSlideProgress, 3.0f);
        float sidebarRight = 420.0f * eased;
        if (mousePos.x > sidebarRight) {
            m_settingsOpen = false;
        }
    }
}

void MainMenuState::renderImGuiPanel() {
    using namespace ui;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
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

    if (m_settingsOpen) {
        flags |= ImGuiWindowFlags_NoInputs;
    }

    ImGui::Begin("##MainMenu", nullptr, flags);

    ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.35f);

    // ── Title ──
    ImGui::SetCursorPos(ImVec2(center.x - 200, center.y - 80));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::SetWindowFontScale(3.0f);
    ImGui::Text("MELODY");
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(center.x - 120, center.y - 20));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 1.0f));
    ImGui::SetWindowFontScale(3.0f);
    ImGui::Text("MATRIX");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);

    // ── Subtitle ──
    ImGui::SetCursorPos(ImVec2(center.x - 100, center.y + 40));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.65f, 1.0f));
    ImGui::Text("A rhythm game experience");
    ImGui::PopStyleColor();

    // ── Buttons ──
    float btnWidth = 280;
    float btnHeight = 50;
    float btnX = center.x - btnWidth / 2;

    ImGui::SetCursorPos(ImVec2(btnX, center.y + 100));
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.65f));
    ImGui::SetWindowFontScale(1.4f);
    if (ImGui::Button("PLAY", ImVec2(btnWidth, btnHeight))) {
        m_nextState = GameState::SongSelect;
    }
    ImGui::PopStyleColor(3);

    ImGui::SetCursorPos(ImVec2(btnX, center.y + 170));
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 0.65f));
    if (ImGui::Button("SETTINGS", ImVec2(btnWidth, btnHeight))) {
        m_settingsOpen = !m_settingsOpen;
    }
    ImGui::PopStyleColor(3);

    ImGui::SetCursorPos(ImVec2(btnX, center.y + 240));
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.65f));
    if (ImGui::Button("QUIT", ImVec2(btnWidth, btnHeight))) {
        Kernel::instance().requestExit();
    }
    ImGui::PopStyleColor(3);

    ImGui::SetWindowFontScale(1.0f);

    // ── 右上角 IMPORT 按钮 ──
    {
        float importBtnWidth = 120;
        float importBtnHeight = 36;
        float importX = displaySize.x - importBtnWidth - 20;
        float importY = 16;

        ImGui::SetCursorPos(ImVec2(importX, importY));
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
        ImGui::SetWindowFontScale(1.0f);
        if (ImGui::Button("IMPORT", ImVec2(importBtnWidth, importBtnHeight))) {
            // 打开文件对话框，选择 .osz 文件
            std::string oszPath = platform::FileDialog::openFile(
                "Select osu! Beatmap Set",
                "osu! Beatmap Archives",
                "osz"
            );
            if (!oszPath.empty()) {
                importOszFile(oszPath);
            }
        }
        ImGui::PopStyleColor(4);
    }

    // ── 导入结果消息（右上角 IMPORT 按钮下方）──
    if (!m_importMessage.empty() && m_importMessageTimer > 0.0f) {
        float msgX = displaySize.x - 340;
        float msgY = 60;
        float alpha = std::min(1.0f, m_importMessageTimer); // 淡出

        ImGui::SetCursorPos(ImVec2(msgX, msgY));
        ImGui::PushStyleColor(ImGuiCol_Text,
            m_importSuccess
                ? ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, alpha)
                : ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, alpha));
        ImGui::SetWindowFontScale(0.85f);
        ImGui::TextWrapped("%s", m_importMessage.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
    }

    // ── Footer ──
    ImGui::SetCursorPos(ImVec2(center.x - 60, displaySize.y - 40));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.5f, 0.6f));
    ImGui::Text("v0.1 alpha");
    ImGui::PopStyleColor();

    ImGui::End();
}

void MainMenuState::renderSettingsOverlay() {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float t = m_settingsSlideProgress;

    float eased = 1.0f - std::pow(1.0f - t, 3.0f);
    float sidebarWidth = 420.0f;
    float sidebarRight = sidebarWidth * eased;

    ImGui::SetNextWindowPos(ImVec2(sidebarRight, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(displaySize.x - sidebarRight, displaySize.y), ImGuiCond_Always);

    ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoNavFocus |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoInputs |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse;

    float dimAlpha = 0.5f * t;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, dimAlpha));
    ImGui::Begin("##SettingsOverlay", nullptr, overlayFlags);
    ImGui::End();
    ImGui::PopStyleColor();
}

void MainMenuState::renderSettingsSidebar() {
    using namespace ui;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float sidebarWidth = 420.0f;

    float t = m_settingsSlideProgress;
    float eased = 1.0f - std::pow(1.0f - t, 3.0f);
    float sidebarX = -sidebarWidth * (1.0f - eased);

    ImGui::SetNextWindowPos(ImVec2(sidebarX, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sidebarWidth, displaySize.y), ImGuiCond_Always);

    ImGuiWindowFlags sidebarFlags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 24));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.133f, 0.133f, 0.227f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.6f));

    ImGui::Begin("##SettingsSidebar", nullptr, sidebarFlags);

    // Neon right-edge border
    {
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float lineX = winPos.x + winSize.x - 1.0f;
        dl->AddLine(ImVec2(lineX, winPos.y), ImVec2(lineX, winPos.y + winSize.y),
            IM_COL32(0, 255, 245, static_cast<int>(60 * t)), 6.0f);
        dl->AddLine(ImVec2(lineX, winPos.y), ImVec2(lineX, winPos.y + winSize.y),
            IM_COL32(0, 255, 245, static_cast<int>(200 * t)), 2.0f);
    }

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::SetWindowFontScale(2.0f);
    ImGui::Text("SETTINGS");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    // Resolution section
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 1.0f));
    ImGui::SetWindowFontScale(1.2f);
    ImGui::Text("RESOLUTION");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();

    const auto& resos = Kernel::supportedResolutions();
    int curW = 0, curH = 0;
    Kernel::instance().getCurrentResolution(curW, curH);
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.65f, 0.8f),
        "Current: %dx%d", curW, curH);
    ImGui::Spacing();

    bool isFullscreen = Kernel::instance().isFullscreen();

    for (int i = 0; i < static_cast<int>(resos.size()); ++i) {
        int nativeW = 0, nativeH = 0;
        Kernel::instance().getNativeResolution(nativeW, nativeH);
        bool isNative = (resos[i].width == nativeW && resos[i].height == nativeH);
        bool isSelected = (resos[i].width == curW && resos[i].height == curH);

        char label[64];
        if (isNative) {
            snprintf(label, sizeof(label), "%s##reso%d",
                     isFullscreen ? "Native (Fullscreen)" : "Native (Borderless)", i);
        } else {
            snprintf(label, sizeof(label), "%s##reso%d", resos[i].label, i);
        }

        if (isFullscreen && !isNative) {
            ImGui::BeginDisabled();
        }

        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.35f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(0.18f, 0.18f, 0.30f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(0.25f, 0.25f, 0.40f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.75f, 1.0f));
        }

        if (ImGui::Button(label, ImVec2(sidebarWidth - 60, 38))) {
            m_pendingResolutionW = resos[i].width;
            m_pendingResolutionH = resos[i].height;
            m_selectedResolutionIndex = i;
        }
        ImGui::PopStyleColor(3);

        if (isFullscreen && !isNative) {
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Fullscreen toggle
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 1.0f));
    ImGui::SetWindowFontScale(1.2f);
    ImGui::Text("DISPLAY");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();

    bool currentFullscreen = Kernel::instance().isFullscreen();
    if (currentFullscreen) {
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.18f, 0.18f, 0.30f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(0.25f, 0.25f, 0.40f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.75f, 1.0f));
    }
    if (ImGui::Button(currentFullscreen ? "FULLSCREEN: ON" : "FULLSCREEN: OFF",
                       ImVec2(sidebarWidth - 60, 38))) {
        m_pendingFullscreen = !currentFullscreen;
        m_hasPendingFullscreen = true;
        if (!currentFullscreen) {
            const auto& resos2 = Kernel::supportedResolutions();
            if (!resos2.empty()) {
                m_pendingResolutionW = resos2[0].width;
                m_pendingResolutionH = resos2[0].height;
            }
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Background dim slider
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 1.0f));
    ImGui::SetWindowFontScale(1.2f);
    ImGui::Text("GAMEPLAY");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();

    {
        static float s_bgDim = platform::Config::getFloat(platform::Config::KEY_BG_DIM, 0.67f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.75f, 1.0f));
        ImGui::Text("Background Dim: %d%%", static_cast<int>(s_bgDim * 100));
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
            ImVec4(0.18f, 0.18f, 0.30f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
            ImVec4(0.25f, 0.25f, 0.40f, 0.9f));
        if (ImGui::SliderFloat("##BgDim", &s_bgDim, 0.0f, 1.0f, "")) {
            Kernel::instance().renderer().setBgDim(s_bgDim);
            platform::Config::setFloat(platform::Config::KEY_BG_DIM, s_bgDim);
            platform::Config::save();
        }
        ImGui::PopStyleColor(4);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    // Close button
    float closeBtnY = displaySize.y - 80;
    ImGui::SetCursorPosY(closeBtnY);
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 1.0f));
    ImGui::SetWindowFontScale(1.2f);
    if (ImGui::Button("CLOSE", ImVec2(sidebarWidth - 60, 42))) {
        m_settingsOpen = false;
    }
    ImGui::PopStyleColor(3);
    ImGui::SetWindowFontScale(1.0f);

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

} // namespace melody_matrix::core
