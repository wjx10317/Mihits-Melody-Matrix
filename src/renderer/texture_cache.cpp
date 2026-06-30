// ============================================================
// texture_cache.cpp — 全局纹理缓存实现（单例）
// ============================================================

#include "texture_cache.h"
#include "util/logger.h"

#include <unordered_set>

namespace melody_matrix::renderer {

TextureCache& TextureCache::instance() {
    static TextureCache s_instance;
    return s_instance;
}

/// 按路径加载；命中缓存直接返回指针
Texture2D* TextureCache::load(const std::string& path, bool genMipmap) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return &it->second;
    }

    Texture2D tex;
    if (tex.loadFromFile(path, genMipmap)) {
        auto result = m_cache.emplace(path, std::move(tex));
        return &result.first->second;
    }

    MM_LOG_WARN("TextureCache", "Failed to load texture: " + path);
    return nullptr;
}

void TextureCache::unload(const std::string& path) {
    m_cache.erase(path);
}

/// 仅查询，不触发加载
Texture2D* TextureCache::get(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return &it->second;
    }
    return nullptr;
}

void TextureCache::preload(const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        load(path, false);
    }
    MM_LOG_INFO("TextureCache", "Preloaded %zu textures, cache size: %zu",
                paths.size(), m_cache.size());
}

void TextureCache::preloadRange(const std::vector<std::string>& paths, int start, int end) {
    if (start < 0) start = 0;
    if (end > static_cast<int>(paths.size())) end = static_cast<int>(paths.size());

    for (int i = start; i < end; ++i) {
        load(paths[i], false);
    }
    MM_LOG_INFO("TextureCache", "Preloaded range [%d, %d), cache size: %zu",
                start, end, m_cache.size());
}

/// 滑动窗口卸载：保留 centerIndex ± radius 范围内的路径
void TextureCache::unloadDistant(const std::vector<std::string>& paths, int centerIndex, int radius) {
    // 构建需要保留的路径集合
    std::unordered_set<std::string> keepPaths;
    int lo = std::max(0, centerIndex - radius);
    int hi = std::min(static_cast<int>(paths.size()) - 1, centerIndex + radius);
    for (int i = lo; i <= hi; ++i) {
        keepPaths.insert(paths[i]);
    }

    // 卸载不在保留集合中的纹理
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (keepPaths.count(it->first) == 0) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

void TextureCache::clear() {
    m_cache.clear();
}

size_t TextureCache::size() const {
    return m_cache.size();
}

} // namespace melody_matrix::renderer
