// ============================================================
// texture_cache.cpp — 全局纹理缓存实现（单例）
// 后台线程 stbi 解码，主线程 uploadFromMemory 上传 GL。
// ============================================================

#include "texture_cache.h"
#include "util/logger.h"

#include <stb_image.h>

#include <algorithm>
#include <filesystem>

namespace melody_matrix::renderer {

std::string TextureCache::normalizeKey(const std::string& path) {
    if (path.empty()) {
        return path;
    }
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        return path;
    }
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, ec);
    return ec ? absolute.string() : canonical.string();
}

void TextureCache::revokePath(const std::string& key) {
    m_revokedPaths.insert(key);
    m_cache.erase(key);
    m_failedPaths.erase(key);
    m_inFlightPaths.erase(key);
}

TextureCache::TextureCache() = default;

TextureCache::~TextureCache() {
    shutdown();
}

TextureCache& TextureCache::instance() {
    static TextureCache s_instance;
    return s_instance;
}

void TextureCache::ensureDecodeThread() {
    if (m_threadStarted) {
        return;
    }
    m_stopDecode = false;
    m_decodeThread = std::thread(&TextureCache::decodeWorkerLoop, this);
    m_threadStarted = true;
}

void TextureCache::decodeWorkerLoop() {
    while (true) {
        DecodeRequest req;
        {
            std::unique_lock lock(m_mutex);
            m_decodeCv.wait(lock, [this] {
                return m_stopDecode.load() || !m_decodeQueue.empty();
            });
            if (m_stopDecode.load() && m_decodeQueue.empty()) {
                break;
            }
            req = std::move(m_decodeQueue.front());
            m_decodeQueue.pop_front();
        }

        {
            std::lock_guard lock(m_mutex);
            if (m_revokedPaths.count(req.path) != 0) {
                m_inFlightPaths.erase(req.path);
                continue;
            }
        }

        stbi_set_flip_vertically_on_load(1);
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(req.path.c_str(), &width, &height, &channels, 0);
        if (!data) {
            MM_LOG_WARN("TextureCache", "Failed to decode: " + req.path + " — " + stbi_failure_reason());
            std::lock_guard lock(m_mutex);
            if (m_revokedPaths.count(req.path) == 0) {
                m_failedPaths.insert(req.path);
            }
            m_inFlightPaths.erase(req.path);
            continue;
        }

        DecodedImage img;
        img.path = req.path;
        img.width = width;
        img.height = height;
        img.channels = channels;
        img.genMipmap = req.genMipmap;
        const size_t pixelCount =
            static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
        img.pixels.assign(data, data + pixelCount);
        stbi_image_free(data);

        {
            std::lock_guard lock(m_mutex);
            if (m_revokedPaths.count(req.path) != 0) {
                m_inFlightPaths.erase(req.path);
                continue;
            }
            m_uploadQueue.push_back(std::move(img));
        }
    }
}

Texture2D* TextureCache::loadSync(const std::string& path, bool genMipmap) {
    const std::string key = normalizeKey(path);
    {
        std::lock_guard lock(m_mutex);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            return &it->second;
        }
        if (m_failedPaths.count(key) != 0) {
            return nullptr;
        }
    }

    Texture2D tex;
    if (!tex.loadFromFile(key, genMipmap)) {
        std::lock_guard lock(m_mutex);
        if (m_revokedPaths.count(key) == 0) {
            m_failedPaths.insert(key);
        }
        m_inFlightPaths.erase(key);
        MM_LOG_WARN("TextureCache", "Failed to load texture: " + key);
        return nullptr;
    }

    std::lock_guard lock(m_mutex);
    if (m_revokedPaths.count(key) != 0) {
        return nullptr;
    }
    m_revokedPaths.erase(key);
    auto result = m_cache.emplace(key, std::move(tex));
    m_inFlightPaths.erase(key);
    return &result.first->second;
}

Texture2D* TextureCache::requestLoad(const std::string& path, bool genMipmap) {
    const std::string key = normalizeKey(path);
    {
        std::lock_guard lock(m_mutex);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            return &it->second;
        }
        if (m_failedPaths.count(key) != 0) {
            return nullptr;
        }
        if (m_inFlightPaths.count(key) != 0) {
            return nullptr;
        }
        m_revokedPaths.erase(key);
        m_inFlightPaths.insert(key);
        m_decodeQueue.push_back({key, genMipmap});
    }

    ensureDecodeThread();
    m_decodeCv.notify_one();
    return nullptr;
}

void TextureCache::processPendingUploads(int maxUploads) {
    if (maxUploads <= 0) {
        return;
    }

    for (int i = 0; i < maxUploads; ++i) {
        DecodedImage img;
        {
            std::lock_guard lock(m_mutex);
            if (m_uploadQueue.empty()) {
                break;
            }
            img = std::move(m_uploadQueue.front());
            m_uploadQueue.pop_front();
        }

        {
            std::lock_guard lock(m_mutex);
            if (m_revokedPaths.count(img.path) != 0) {
                m_inFlightPaths.erase(img.path);
                continue;
            }
        }

        Texture2D tex;
        if (img.pixels.empty() ||
            !tex.uploadFromMemory(img.pixels.data(), img.width, img.height, img.channels,
                                  img.genMipmap, img.path)) {
            std::lock_guard lock(m_mutex);
            if (m_revokedPaths.count(img.path) == 0) {
                m_failedPaths.insert(img.path);
            }
            m_inFlightPaths.erase(img.path);
            MM_LOG_WARN("TextureCache", "Failed to upload texture: " + img.path);
            continue;
        }

        {
            std::lock_guard lock(m_mutex);
            if (m_revokedPaths.count(img.path) != 0) {
                m_inFlightPaths.erase(img.path);
                continue;
            }
            m_revokedPaths.erase(img.path);
            m_cache.emplace(img.path, std::move(tex));
            m_inFlightPaths.erase(img.path);
        }
    }
}

void TextureCache::unload(const std::string& path) {
    std::lock_guard lock(m_mutex);
    revokePath(normalizeKey(path));
}

Texture2D* TextureCache::get(const std::string& path) {
    std::lock_guard lock(m_mutex);
    auto it = m_cache.find(normalizeKey(path));
    if (it != m_cache.end()) {
        return &it->second;
    }
    return nullptr;
}

bool TextureCache::isLoaded(const std::string& path) const {
    std::lock_guard lock(m_mutex);
    auto it = m_cache.find(normalizeKey(path));
    return it != m_cache.end() && it->second.valid();
}

bool TextureCache::hasFailed(const std::string& path) const {
    std::lock_guard lock(m_mutex);
    return m_failedPaths.count(normalizeKey(path)) != 0;
}

bool TextureCache::isPending(const std::string& path) const {
    std::lock_guard lock(m_mutex);
    return m_inFlightPaths.count(normalizeKey(path)) != 0;
}

void TextureCache::preload(const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        requestLoad(path, false);
    }
    MM_LOG_INFO("TextureCache", "Requested preload for %zu textures", paths.size());
}

void TextureCache::preloadRange(const std::vector<std::string>& paths, int start, int end) {
    if (start < 0) {
        start = 0;
    }
    if (end > static_cast<int>(paths.size())) {
        end = static_cast<int>(paths.size());
    }

    for (int i = start; i < end; ++i) {
        requestLoad(paths[static_cast<size_t>(i)], false);
    }
    MM_LOG_INFO("TextureCache", "Requested preload range [%d, %d)", start, end);
}

void TextureCache::unloadDistant(const std::vector<std::string>& paths, int centerIndex, int radius) {
    // 只卸载 paths 中远离 center 的条目；绝不能清掉 cache 里其它纹理
    // （note 皮肤也走 TextureCache，误删会导致 NoteRenderer 悬空指针 → 纯黑）。
    if (paths.empty()) {
        return;
    }

    const int lo = std::max(0, centerIndex - radius);
    const int hi = std::min(static_cast<int>(paths.size()) - 1, centerIndex + radius);

    std::unordered_set<std::string> keepKeys;
    keepKeys.reserve(static_cast<size_t>(std::max(0, hi - lo + 1)));
    for (int i = lo; i <= hi; ++i) {
        keepKeys.insert(normalizeKey(paths[static_cast<size_t>(i)]));
    }

    std::unordered_set<std::string> revokeKeys;
    for (int i = 0; i < static_cast<int>(paths.size()); ++i) {
        if (i >= lo && i <= hi) {
            continue;
        }
        const std::string key = normalizeKey(paths[static_cast<size_t>(i)]);
        if (key.empty() || keepKeys.count(key) != 0) {
            continue;
        }
        revokeKeys.insert(key);
    }

    std::lock_guard lock(m_mutex);
    for (const auto& key : revokeKeys) {
        revokePath(key);
    }
}

void TextureCache::shutdown() {
    {
        std::lock_guard lock(m_mutex);
        m_stopDecode = true;
        m_decodeCv.notify_all();
    }

    if (m_threadStarted && m_decodeThread.joinable()) {
        m_decodeThread.join();
        m_threadStarted = false;
    }

    std::lock_guard lock(m_mutex);
    m_cache.clear();
    m_failedPaths.clear();
    m_inFlightPaths.clear();
    m_revokedPaths.clear();
    m_decodeQueue.clear();
    m_uploadQueue.clear();
    m_stopDecode = false;
}

void TextureCache::clear() {
    shutdown();
}

size_t TextureCache::size() const {
    std::lock_guard lock(m_mutex);
    return m_cache.size();
}

} // namespace melody_matrix::renderer
