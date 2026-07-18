// ============================================================
// main_menu_state.cpp — 主菜单状态实现
//
// 职责：
//   - 渲染标题屏幕与导航按钮
//   - 管理设置侧边栏（分辨率 / 全屏）
//   - 导入 .osz 铺面并转换为 .mma 格式
// ============================================================
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

namespace {

/// 异步预加载歌曲目录内的 background.*（与 scanBeatmaps 命名规则一致）
void requestLoadBeatmapBackground(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return;
    }
    for (const auto& f : fs::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        std::string fileExt = f.path().extension().string();
        for (char& c : fileExt) {
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
        if (fileExt != ".png" && fileExt != ".jpg" && fileExt != ".jpeg" && fileExt != ".webp") {
            continue;
        }
        if (f.path().stem().string() != "background") {
            continue;
        }
        renderer::TextureCache::instance().requestLoad(fs::absolute(f.path()).string(), false);
    }
}

} // namespace

// ──────────────────────────────────────────────────────
//  辅助函数
// ──────────────────────────────────────────────────────

/// 替换文件名中 Windows 不允许的字符（osu Title/Version → 安全目录名）
static std::string sanitizeFilename(const std::string& name) {
    std::string result = name;
    for (auto& c : result) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';                                         // 非法字符替换为下划线
        }
    }
    while (!result.empty() && result.front() == ' ') result.erase(result.begin());  // 去首尾空格
    while (!result.empty() && result.back() == ' ') result.pop_back();
    if (result.empty()) result = "Unknown";                  // 空标题兜底
    if (result.size() > 200) result = result.substr(0, 200); // 限制长度防路径过长
    return result;
}

// ──────────────────────────────────────────────────────
//  生命周期
// ──────────────────────────────────────────────────────

// ══════════════════════════════════════════════════════════════════════════════
//  初始化
// ══════════════════════════════════════════════════════════════════════════════

/// 尝试从多个相对路径加载菜单背景 JPG
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

/// 进入主菜单：重置 UI 状态、加载背景与已导入哈希
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

    // 构建已导入哈希集合：仅在空表时从磁盘扫描一次（与 import 去重联动；不在删谱后全量重扫）
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

/// 退出主菜单
void MainMenuState::onExit() {
    MM_LOG_INFO("MainMenu", "Exiting Main Menu");
}

// ══════════════════════════════════════════════════════════════════════════════
//  更新
// ══════════════════════════════════════════════════════════════════════════════

/// 每帧更新：应用延迟分辨率变更、侧边栏滑入动画、导入消息淡出
GameState MainMenuState::update(float dt) {
    // 应用延迟的分辨率 / 全屏变更
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

    // 设置侧边栏滑入/滑出动画
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

/// 导入单个 .osz（委托批量入口）
void MainMenuState::importOszFile(const std::string& oszPath) {
    if (oszPath.empty()) {
        return;
    }
    importOszFiles({oszPath});
}

/// 批量导入多个 .osz，汇总 UI 消息
void MainMenuState::importOszFiles(const std::vector<std::string>& oszPaths) {
    using namespace util;
    if (oszPaths.empty()) {
        return;
    }

    // 确保铺面根目录存在（删光时若误删 assets/beatmaps，create 会失败表现为「导入失败」）
    try {
        std::filesystem::create_directories("assets/beatmaps");
    } catch (const std::exception& e) {
        m_importSuccess = false;
        m_importMessageTimer = 5.0f;
        m_importMessage = std::string("Import failed: cannot create beatmaps dir: ") + e.what();
        return;
    }

    int totalImported = 0;
    int totalSkipped = 0;
    int failedArchives = 0;
    std::string lastError;

    for (const auto& oszPath : oszPaths) {
        int imported = 0;
        int skipped = 0;
        auto result = validateAndImportOsz(oszPath, &imported, &skipped);
        totalImported += imported;
        totalSkipped += skipped;
        if (!result.ok()) {
            if (result.error().code ==
                static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_ALREADY_IMPORTED)) {
                continue;
            }
            ++failedArchives;
            lastError = result.error().message;
            MM_LOG_WARN("Import", "Archive failed: %s (%s)",
                        oszPath.c_str(), lastError.c_str());
        }
    }

    if (totalImported > 0) {
        m_importSuccess = true;
        m_importMessageTimer = 3.0f;
        m_importMessage = "Imported " + std::to_string(totalImported) + " beatmap(s)";
        if (oszPaths.size() > 1) {
            m_importMessage += " from " + std::to_string(oszPaths.size()) + " archive(s)";
        }
        if (totalSkipped > 0) {
            m_importMessage += " (" + std::to_string(totalSkipped) + " already exists)";
        }
        if (failedArchives > 0) {
            m_importMessage += " [" + std::to_string(failedArchives) + " archive(s) failed]";
        }
        return;
    }

    if (totalSkipped > 0 && failedArchives == 0) {
        m_importMessage.clear();
        return;
    }

    m_importSuccess = false;
    m_importMessageTimer = 5.0f;
    if (failedArchives > 0) {
        m_importMessage = "Import failed: " +
            (lastError.empty() ? (std::to_string(failedArchives) + " archive(s) failed")
                               : lastError);
    } else {
        m_importMessage = "Import failed: no beatmaps imported";
    }
}

/// 校验 .osz 扩展名、解压到临时目录并逐个导入 .osu（osu! 谱面集 → 本地 .mma）
util::Result<void> MainMenuState::validateAndImportOsz(const std::string& oszPath,
                                                       int* outImported, int* outSkipped) {
    using namespace util;
    if (outImported) {
        *outImported = 0;
    }
    if (outSkipped) {
        *outSkipped = 0;
    }

    // ── V1: 文件扩展名必须为 .osz（osu! 谱面集 zip 包）──
    std::string ext;
    {
        auto dotPos = oszPath.rfind('.');                  // 找最后一个点，定位扩展名
        if (dotPos == std::string::npos) {                   // 无扩展名则拒绝
            return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_IO),
                                "File has no extension: " + oszPath);
        }
        ext = oszPath.substr(dotPos + 1);                  // 截取扩展名子串
        for (auto& c : ext) c = static_cast<char>(tolower(c));  // 转小写，兼容 .OSZ
        if (ext != "osz") {                                  // 非 osz 格式直接报错
            return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_IO),
                                "Not an .osz file: " + oszPath);
        }
    }

    // ── V2: 文件可读（基本存在性检查）──
    if (!std::filesystem::exists(oszPath)) {                 // 路径不存在
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_FILE_NOT_FOUND),
                            "File not found: " + oszPath);
    }

    // ── V2.5: 解压 .osz 到系统临时目录（osz 本质是 zip）──
    std::string tempDir = platform::ZipExtract::createTempDir();  // 创建唯一临时文件夹
    if (tempDir.empty()) {                                   // 创建失败（磁盘/权限问题）
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_IO),
                            "Cannot create temp directory for extraction");
    }

    // RAII 清理：函数退出时（成功/失败均）删除临时目录，避免泄漏
    struct TempDirGuard {
        std::string path;
        ~TempDirGuard() { platform::ZipExtract::removeDir(path); }
    } tempDirGuard{tempDir};

    if (!platform::ZipExtract::extract(oszPath, tempDir)) {  // 解压 zip 到 tempDir
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_INVALID_ARCHIVE),
                            "Failed to extract .osz file");
    }

    // ── V2.6: 递归查找解压目录内全部 .osu 文件（一个 osz 可含多个难度）──
    auto osuFiles = platform::ZipExtract::findOsuFiles(tempDir);
    if (osuFiles.empty()) {                                  // 空包或无 .osu
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_NO_OSU_IN_ARCHIVE),
                            "No .osu files found in .osz archive");
    }

    MM_LOG_INFO("Import", "Found %zu .osu files in archive", osuFiles.size());

    // ── 逐个处理 .osu 文件（每个 .osu → 一个 .mma 难度）──
    int importedCount = 0;                                   // 本次新导入数量
    int skippedCount = 0;                                  // SHA256 重复跳过数量
    std::string lastError;                                   // 记录最后一个失败原因

    for (const auto& osuPath : osuFiles) {                   // 遍历每个 .osu 路径
        auto singleResult = importSingleOsu(osuPath, tempDir);  // 单文件解析→写入 assets
        if (singleResult.ok()) {                             // 该难度导入成功
            ++importedCount;
        } else if (singleResult.error().code ==
                   static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_ALREADY_IMPORTED)) {
            ++skippedCount;                                  // 内容哈希已存在，跳过
        } else {
            // 记录错误但继续处理其他 .osu（一个难度失败不影响同包其他难度）
            lastError = singleResult.error().message;
            MM_LOG_WARN("Import", "Failed to import %s: %s",
                        osuPath.c_str(), lastError.c_str());
        }
    }

    if (outImported) {
        *outImported = importedCount;
    }
    if (outSkipped) {
        *outSkipped = skippedCount;
    }

    if (importedCount > 0) {
        return success();
    }
    if (skippedCount > 0) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_ALREADY_IMPORTED), "");
    }
    return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_PARSE),
                        lastError.empty() ? "All .osu files failed to import" : lastError);
}

/// 解析单个 .osu、构建 Beatmap、序列化为 .mma 并写入 assets/beatmaps/（单难度导入核心）
util::Result<void> MainMenuState::importSingleOsu(const std::string& osuPath, const std::string& extractRoot) {
    using namespace util;

    // ── V3: 读取 .osu 全文并检查 osu! 格式头 ──
    std::string content;
    {
        auto readResult = platform::FileSystem::readFile(osuPath);  // 从解压临时目录读取
        if (!readResult.ok()) {                                // 文件不可读
            return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_FILE_NOT_FOUND),
                                "Cannot read .osu: " + osuPath);
        }
        content = readResult.value();                          // 保存完整文本供后续解析
    }

    {
        auto firstNewline = content.find('\n');                  // 第一行必须是格式声明
        std::string firstLine = content.substr(0, firstNewline);
        if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();  // 去 CRLF
        if (firstLine.find("osu file format v") == std::string::npos) {  // 非 osu 标准头
            return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_INVALID_HEADER),
                                "Invalid .osu header: " + firstLine);
        }
    }

    // ── V4: Mode 检查（仅 standard，Mode=0；其他模式如 taiko/mania 不支持）──
    {
        auto generalPos = content.find("[General]");             // 定位 General 段
        if (generalPos != std::string::npos) {
            auto nextSection = content.find('[', generalPos + 9);  // 下一段 [ 起始
            std::string generalSection = content.substr(generalPos,
                nextSection == std::string::npos ? std::string::npos : nextSection - generalPos);
            auto modePos = generalSection.find("Mode:");         // 兼容 Mode: 写法
            if (modePos == std::string::npos) {
                modePos = generalSection.find("Mode=");          // 兼容 Mode= 写法
            }
            if (modePos != std::string::npos) {                  // 找到 Mode 字段
                auto lineEnd = generalSection.find('\n', modePos);
                std::string modeLine = generalSection.substr(modePos, lineEnd - modePos);
                auto colonPos = modeLine.find(':');
                auto equalsPos = modeLine.find('=');
                auto sepPos = (colonPos != std::string::npos) ? colonPos : equalsPos;
                if (sepPos != std::string::npos) {
                    std::string modeVal = modeLine.substr(sepPos + 1);  // 取值部分
                    while (!modeVal.empty() && (modeVal.front() == ' ' || modeVal.front() == '\t'))
                        modeVal.erase(modeVal.begin());          // 去前导空白
                    while (!modeVal.empty() && (modeVal.back() == ' ' || modeVal.back() == '\t' ||
                           modeVal.back() == '\r'))
                        modeVal.pop_back();                      // 去尾随空白/回车
                    if (modeVal != "0") {                        // 非 standard
                        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_NOT_STANDARD),
                                            "Only osu!standard (Mode=0) is supported, got Mode=" + modeVal);
                    }
                }
            }
            // Mode 不存在默认为 0 (standard)
        }
    }

    // ── SHA256 去重：相同 .osu 内容不重复写入（跨 osz 导入也生效）──
    std::string fileHash = util::sha256(content.data(), content.size());
    if (m_importedHashes.count(fileHash) > 0) {                // 哈希已在集合中
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_ALREADY_IMPORTED), "");
    }

    // ── V5: 用 osu 解析器解析文本 → BeatmapBuilder 中间结构 ──
    auto parser = beatmap::createParserForFile(osuPath);       // 根据扩展名选 osu 解析器
    beatmap::BeatmapBuilder builder;
    auto parseResult = parser->parse(content, builder);        // 解析 hitobjects/timing 等
    if (!parseResult.ok()) {                                   // 语法/格式错误
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_PARSE),
                            "Parse error: " + parseResult.error().message);
    }

    // ── V6: build 校验并生成最终 Beatmap（含 note 列表、难度、元数据）──
    auto buildResult = builder.build();
    if (!buildResult.ok()) {                                   // 业务校验失败（如非法 note）
        return Result<void>(buildResult.error().code,
                            "Build error: " + buildResult.error().message);
    }

    auto& beatmap = buildResult.value();
    if (beatmap.notes.empty()) {                               // 空谱面无游玩价值
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_EMPTY_NOTES),
                            "Beatmap has no notes");
    }

    // ── V7: 必须有 AudioFilename 引用（否则无法播放 BGM）──
    if (beatmap.meta.audioFile.empty()) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_NO_AUDIO_REF),
                            "Beatmap has no audio file reference");
    }

    // ── V8: Title 非空（用于目录名与 UI 显示）──
    if (beatmap.meta.title.empty()) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_BEATMAP_NO_TITLE),
                            "Beatmap title is empty");
    }

    // ── V11: Version 空则默认填充 ──
    if (beatmap.meta.version.empty()) {
        beatmap.meta.version = "Normal";                       // osu 有时不写 Version
    }

    // ── 构建目标路径：assets/beatmaps/{Title}/{Version}.mma ──
    std::string safeTitle = sanitizeFilename(beatmap.meta.title);    // 去掉非法文件名字符
    std::string safeVersion = sanitizeFilename(beatmap.meta.version);
    std::filesystem::path targetDir = std::filesystem::path("assets/beatmaps") / safeTitle;
    std::filesystem::path targetPath = targetDir / (safeVersion + ".mma");

    // ── 创建歌曲目录 ──
    try {
        std::filesystem::create_directories(targetDir);        // 递归创建，已存在则忽略
    } catch (const std::exception& e) {
        return Result<void>(static_cast<int32_t>(ErrorCode::ERROR_IO),
                            std::string("Cannot create directory: ") + e.what());
    }

    // ── 写入 .mma 文件（内含 sourceHash 供日后去重）──
    auto writeResult = beatmap::MmaSerializer::writeToFile(beatmap, targetPath.string(), fileHash);
    if (!writeResult.ok()) {
        return writeResult;                                    // 磁盘写入失败
    }

    // ── V9: 复制音频文件（从解压目录到目标歌曲目录）──
    {
        // osuPath 在解压临时目录中，音频文件通常与 .osu 同目录或包根目录
        std::filesystem::path osuDir = std::filesystem::path(osuPath).parent_path();
        std::filesystem::path audioSrc = osuDir / beatmap.meta.audioFile;  // 优先同目录
        std::filesystem::path audioDst = targetDir / beatmap.meta.audioFile;  // 保持相对路径

        // 也搜索解压根目录（有些 .osz 的音频在根目录，.osu 在子目录）
        if (!std::filesystem::exists(audioSrc)) {
            std::filesystem::path rootAudioSrc = std::filesystem::path(extractRoot) / beatmap.meta.audioFile;
            if (std::filesystem::exists(rootAudioSrc)) {
                audioSrc = rootAudioSrc;                       // 回退到包根目录
            }
        }

        if (audioDst.has_parent_path()) {
            std::filesystem::create_directories(audioDst.parent_path());  // 音频可能在子文件夹
        }

        if (std::filesystem::exists(audioSrc)) {
            if (!std::filesystem::exists(audioDst)) {          // 已存在则不覆盖（同包多难度共享音频）
                std::filesystem::copy_file(audioSrc, audioDst,
                    std::filesystem::copy_options::overwrite_existing);
                MM_LOG_INFO("Import", "Audio copied: %s", beatmap.meta.audioFile.c_str());
            }
        } else {
            MM_LOG_WARN("Import", "Audio not found in archive: %s", beatmap.meta.audioFile.c_str());
            // 音频缺失仍保留 .mma，游玩时可能 fallback 到 assets
        }
    }

    // ── V10: 复制背景图（从 [Events] 段解析 BG 事件）→ 重命名为 background.扩展名 ──
    {
        auto eventsPos = content.find("[Events]");               // osu 背景在 Events 段
        if (eventsPos != std::string::npos) {
            auto nextSection = content.find('[', eventsPos + 8);
            std::string eventsSection = content.substr(eventsPos,
                nextSection == std::string::npos ? std::string::npos : nextSection - eventsPos);

            std::istringstream ess(eventsSection);               // 逐行扫描
            std::string line;
            while (std::getline(ess, line)) {
                if (line.empty() || line[0] == '[' || line[0] == '/' || line[0] == '_') continue;  // 跳过段头/注释

                auto q1 = line.find('"');                        // BG 事件格式含引号包文件名
                if (q1 != std::string::npos) {
                    auto q2 = line.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        std::string bgFile = line.substr(q1 + 1, q2 - q1 - 1);  // 提取背景文件名
                        if (!bgFile.empty()) {
                            // 在 .osu 同目录和解压根目录搜索
                            std::filesystem::path bgSrc = std::filesystem::path(osuPath).parent_path() / bgFile;
                            if (!std::filesystem::exists(bgSrc)) {
                                bgSrc = std::filesystem::path(extractRoot) / bgFile;
                            }

                            if (std::filesystem::exists(bgSrc)) {
                                // 目标文件名固定为 background + 原始后缀（与 scanBeatmaps 查找规则一致）
                                std::string bgExt = std::filesystem::path(bgFile).extension().string();
                                std::filesystem::path bgDst = targetDir / ("background" + bgExt);

                                if (!std::filesystem::exists(bgDst)) {  // 不覆盖已有背景
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
                            break;                               // 只处理第一个 BG 事件
                        }
                    }
                }
            }
        }
    }

    // ── 注册哈希到内存集合（与 onEnter 扫描的 .mma 内 hash 共同构成去重集）──
    m_importedHashes.insert(fileHash);

    const std::string mmaPath = std::filesystem::absolute(targetPath).string();
    if (auto* songSelect = Kernel::instance().stateManager().getStateAs<SongSelectState>(GameState::SongSelect)) {
        songSelect->registerImportedMma(mmaPath);
    }

    // 背景图复制完成后异步预热 TextureCache（registerImportedMma 也会 requestLoad imagePath）
    requestLoadBeatmapBackground(targetDir);

    MM_LOG_INFO("Import", "Successfully imported: %s -> %s", osuPath.c_str(), targetPath.string().c_str());
    return success();
}

// ══════════════════════════════════════════════════════════════════════════════
//  渲染
// ══════════════════════════════════════════════════════════════════════════════

/// 渲染顺序：背景 → 主面板 → 设置遮罩 → 设置侧边栏（后者在最上层）
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

    // 渲染顺序：MainMenu → Overlay → Sidebar（后者在最上层）
    renderImGuiPanel();
    if (m_settingsSlideProgress > 0.001f) {
        renderSettingsOverlay();
        renderSettingsSidebar();
    }

    // 点击侧边栏外部区域时关闭设置
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

/// 渲染主菜单 ImGui 面板：标题、PLAY/SETTINGS/QUIT 按钮与 IMPORT
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

    const float s = Theme::displayScale();
    ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.35f);

    // ── 标题 ──
    ImGui::SetCursorPos(ImVec2(center.x - 200.0f * s, center.y - 80.0f * s));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
    ImGui::SetWindowFontScale(3.0f);
    ImGui::Text("MELODY");
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(center.x - 120.0f * s, center.y - 20.0f * s));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::PURP_R, Theme::PURP_G, Theme::PURP_B, 1.0f));
    ImGui::SetWindowFontScale(3.0f);
    ImGui::Text("MATRIX");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);

    // ── 副标题 ──
    ImGui::SetCursorPos(ImVec2(center.x - 100.0f * s, center.y + 40.0f * s));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.65f, 1.0f));
    ImGui::Text("A rhythm game experience");
    ImGui::PopStyleColor();

    // ── 按钮 ──
    float btnWidth = 280.0f * s;
    float btnHeight = 50.0f * s;
    float btnX = center.x - btnWidth / 2;

    ImGui::SetCursorPos(ImVec2(btnX, center.y + 100.0f * s));
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

    ImGui::SetCursorPos(ImVec2(btnX, center.y + 170.0f * s));
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

    ImGui::SetCursorPos(ImVec2(btnX, center.y + 240.0f * s));
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
        float importBtnWidth = 120.0f * s;
        float importBtnHeight = 36.0f * s;
        float importX = displaySize.x - importBtnWidth - 20.0f * s;
        float importY = 16.0f * s;

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
            // 多选 .osz 谱面集（Ctrl/Shift 批量）
            auto oszPaths = platform::FileDialog::openFiles(
                "Select osu! Beatmap Set(s)",
                "osu! Beatmap Archives",
                "osz"
            );
            if (!oszPaths.empty()) {
                importOszFiles(oszPaths);
            }
        }
        ImGui::PopStyleColor(4);
    }

    // ── 导入结果消息（右上角 IMPORT 按钮下方）──
    if (!m_importMessage.empty() && m_importMessageTimer > 0.0f) {
        float msgX = displaySize.x - 340.0f * s;
        float msgY = 60.0f * s;
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

    // ── 页脚版本号 ──
    ImGui::SetCursorPos(ImVec2(center.x - 60.0f * s, displaySize.y - 40.0f * s));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.5f, 0.6f));
    ImGui::Text("v0.1 alpha");
    ImGui::PopStyleColor();

    ImGui::End();
}

/// 渲染设置侧边栏右侧的半透明遮罩（随滑入进度渐变）
void MainMenuState::renderSettingsOverlay() {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float t = m_settingsSlideProgress;

    float eased = 1.0f - std::pow(1.0f - t, 3.0f);
    float sidebarWidth = 420.0f * ui::Theme::displayScale();
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

/// 渲染左侧滑入的设置侧边栏（分辨率、全屏、音量等选项）
void MainMenuState::renderSettingsSidebar() {
    using namespace ui;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float s = Theme::displayScale();
    float sidebarWidth = 420.0f * s;

    float t = m_settingsSlideProgress;
    float eased = 1.0f - std::pow(1.0f - t, 3.0f);
    float sidebarX = -sidebarWidth * (1.0f - eased);

    ImGui::SetNextWindowPos(ImVec2(sidebarX, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sidebarWidth, displaySize.y), ImGuiCond_Always);

    ImGuiWindowFlags sidebarFlags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f * s, 24.0f * s));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.133f, 0.133f, 0.227f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.6f));

    ImGui::Begin("##SettingsSidebar", nullptr, sidebarFlags);

    // 右侧霓虹描边
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

        if (ImGui::Button(label, ImVec2(sidebarWidth - 60.0f * s, 38.0f * s))) {
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
                       ImVec2(sidebarWidth - 60.0f * s, 38.0f * s))) {
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

    // 判定时间偏移：音频听感偏晚时增大，使判定早于歌曲时间
    {
        static int s_timingOffset = platform::Config::getInt(
            platform::Config::KEY_TIMING_OFFSET, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.75f, 1.0f));
        ImGui::Text("Timing Offset: %+d ms", s_timingOffset);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.6f, 0.9f));
        ImGui::TextWrapped("Positive = judge earlier if audio feels late.");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
            ImVec4(Theme::CYAN_R, Theme::CYAN_G, Theme::CYAN_B, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
            ImVec4(0.18f, 0.18f, 0.30f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
            ImVec4(0.25f, 0.25f, 0.40f, 0.9f));
        if (ImGui::SliderInt("##TimingOffset", &s_timingOffset, -100, 100, "%d ms")) {
            platform::Config::setInt(platform::Config::KEY_TIMING_OFFSET, s_timingOffset);
            platform::Config::save();
        }
        ImGui::PopStyleColor(4);
    }

    ImGui::Spacing();

    // Debug HUD 开关
    {
        bool debugHud = platform::Config::getInt(platform::Config::KEY_DEBUG_HUD, 0) != 0;
        if (debugHud) {
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
        if (ImGui::Button(debugHud ? "DEBUG HUD: ON" : "DEBUG HUD: OFF",
                          ImVec2(sidebarWidth - 60.0f * s, 38.0f * s))) {
            platform::Config::setInt(platform::Config::KEY_DEBUG_HUD, debugHud ? 0 : 1);
            platform::Config::save();
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();

    // 偏移条开关（Offset Bar）
    {
        bool offsetBarEnabled = platform::Config::getInt(platform::Config::KEY_OFFSET_BAR, 0) != 0;
        if (offsetBarEnabled) {
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
        if (ImGui::Button(offsetBarEnabled ? "OFFSET BAR: ON" : "OFFSET BAR: OFF",
                          ImVec2(sidebarWidth - 60.0f * s, 38.0f * s))) {
            platform::Config::setInt(platform::Config::KEY_OFFSET_BAR, offsetBarEnabled ? 0 : 1);
            platform::Config::save();
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    // Close button
    float closeBtnY = displaySize.y - 80.0f * s;
    ImGui::SetCursorPosY(closeBtnY);
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImVec4(Theme::PINK_R, Theme::PINK_G, Theme::PINK_B, 1.0f));
    ImGui::SetWindowFontScale(1.2f);
    if (ImGui::Button("CLOSE", ImVec2(sidebarWidth - 60.0f * s, 42.0f * s))) {
        m_settingsOpen = false;
    }
    ImGui::PopStyleColor(3);
    ImGui::SetWindowFontScale(1.0f);

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

} // namespace melody_matrix::core
