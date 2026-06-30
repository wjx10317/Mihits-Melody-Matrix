#pragma once

// ============================================================
// texture_cache.h — 全局纹理缓存（单例）
// 以文件路径为键缓存 Texture2D，避免重复磁盘加载；
// 支持批量预加载与滑动窗口卸载（选歌列表等场景）。
// ============================================================

#include "renderer/texture.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace melody_matrix::renderer {

/// 全局纹理缓存，生命周期贯穿整个应用。
class TextureCache {
public:
    static TextureCache& instance();

    /// 加载或返回已缓存纹理
    Texture2D* load(const std::string& path, bool genMipmap = false);

    /// 仅查询缓存，未命中返回 nullptr
    Texture2D* get(const std::string& path);

    /// 卸载指定路径的缓存纹理
    void unload(const std::string& path);

    /// 批量预加载路径列表
    void preload(const std::vector<std::string>& paths);

    /// 部分预加载 [start, end) 范围（窗口化预加载预留接口）
    void preloadRange(const std::vector<std::string>& paths, int start, int end);

    /// 滑动窗口卸载：保留 centerIndex ± radius，其余 erase
    void unloadDistant(const std::vector<std::string>& paths, int centerIndex, int radius);

    void clear();
    size_t size() const;

private:
    TextureCache() = default;
    std::unordered_map<std::string, Texture2D> m_cache;
};

} // namespace melody_matrix::renderer
