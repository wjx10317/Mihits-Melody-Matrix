#pragma once

#include "renderer/texture.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace melody_matrix::renderer {

/// 全局纹理缓存，支持跨状态持久化和部分预加载。
/// 以文件路径为键缓存 Texture2D，避免重复加载。
/// 生命周期贯穿整个应用，纹理在状态切换时不会被释放。
class TextureCache {
public:
    /// 获取单例实例
    static TextureCache& instance();

    /// 按路径加载纹理（如已缓存则直接返回指针）
    /// @param path     图像文件路径
    /// @param genMipmap 是否生成 mipmap（默认 false，用于 ImGui 显示）
    /// @return 成功返回纹理指针，失败返回 nullptr
    Texture2D* load(const std::string& path, bool genMipmap = false);

    /// 按路径获取已缓存的纹理（未缓存返回 nullptr）
    Texture2D* get(const std::string& path);

    /// 批量预加载指定路径列表的纹理
    void preload(const std::vector<std::string>& paths);

    /// 部分预加载：仅加载 [start, end) 范围内的纹理。
    /// 预留接口，用于未来实现"窗口部分预加载"——当铺面列表数量增加时，
    /// 仅加载当前可视区域及附近的图片资源，减少内存占用。
    /// @param paths  所有路径列表
    /// @param start  起始索引（含）
    /// @param end    结束索引（不含）
    void preloadRange(const std::vector<std::string>& paths, int start, int end);

    /// 卸载不在指定索引范围附近的纹理（滑动窗口策略）。
    /// 保留 centerIndex ± radius 范围内的纹理，其余卸载。
    /// @param paths        所有路径列表（与铺面分组一一对应）
    /// @param centerIndex  当前居中位置索引
    /// @param radius       保留范围
    void unloadDistant(const std::vector<std::string>& paths, int centerIndex, int radius);

    /// 清空所有缓存
    void clear();

    /// 获取缓存中的纹理数量
    size_t size() const;

private:
    TextureCache() = default;
    std::unordered_map<std::string, Texture2D> m_cache;
};

} // namespace melody_matrix::renderer
