#pragma once

#include <cstdint>
#include <string>

namespace melody_matrix::renderer {

/// OpenGL 2D 纹理的 RAII 包装器。
/// 通过 stb_image 加载图像文件，创建 GL 纹理，销毁时自动清理。
class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();

    // 仅可移动（RAII）
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    /// 加载图像文件并创建 OpenGL 纹理。
    /// 通过 stb_image 支持 JPEG、PNG、BMP、TGA 等格式。
    /// @param path  图像的文件系统路径
    /// @param genMipmap  是否生成 mipmap（默认：true）
    /// @return 成功返回 true，失败返回 false
    bool loadFromFile(const std::string& path, bool genMipmap = true);

    /// 将此纹理绑定到指定的纹理单元
    /// @param unit  纹理单元索引（0 = GL_TEXTURE0 等）
    void bind(uint32_t unit = 0) const;

    /// 从指定单元解绑纹理
    static void unbind(uint32_t unit = 0);

    /// 获取 OpenGL 纹理 ID
    uint32_t textureId() const { return m_textureId; }

    /// 获取图像宽度（像素）
    int32_t width() const { return m_width; }

    /// 获取图像高度（像素）
    int32_t height() const { return m_height; }

    /// 获取颜色通道数（3=RGB，4=RGBA）
    int32_t channels() const { return m_channels; }

    /// 检查纹理是否有效（加载成功）
    bool valid() const { return m_textureId != 0; }

private:
    void release();

    uint32_t m_textureId = 0;
    int32_t m_width = 0;
    int32_t m_height = 0;
    int32_t m_channels = 0;
};

} // namespace melody_matrix::renderer
