#pragma once

// ============================================================
// texture.h — OpenGL 2D 纹理 RAII 包装
// 通过 stb_image 加载图像，创建 GL 纹理；析构时自动 glDeleteTextures。
// ============================================================

#include <cstdint>   // uint32_t, int32_t
#include <string>    // std::string

namespace melody_matrix::renderer {

/// OpenGL 2D 纹理的 RAII 包装器。
/// 通过 stb_image 加载图像文件，创建 GL 纹理，销毁时自动清理。
class Texture2D {
public:
    Texture2D() = default;   // 默认构造：无有效 GL 纹理
    ~Texture2D();            // 析构时 release()

    // 仅可移动（RAII），禁止拷贝以防双重释放
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    /// 加载图像并上传为 GL_TEXTURE_2D（JPEG/PNG/BMP/TGA 等）
    /// @param genMipmap  是否生成 mipmap（note 纹理建议 true）
    bool loadFromFile(const std::string& path, bool genMipmap = true);

    /// 从已解码像素上传 GPU（主线程调用；供 TextureCache 异步 decode 队列使用）
    bool uploadFromMemory(const unsigned char* data, int width, int height, int channels,
                          bool genMipmap, const std::string& debugLabel = "");

    /// 绑定到指定纹理单元（0 = GL_TEXTURE0）
    void bind(uint32_t unit = 0) const;

    /// 从指定单元解绑
    static void unbind(uint32_t unit = 0);

    uint32_t textureId() const { return m_textureId; }  ///< OpenGL 纹理对象 ID
    int32_t width() const { return m_width; }          ///< 像素宽度
    int32_t height() const { return m_height; }        ///< 像素高度
    int32_t channels() const { return m_channels; }    ///< 通道数（3=RGB, 4=RGBA）
    bool valid() const { return m_textureId != 0; }    ///< 是否已成功加载

private:
    void release();  ///< 删除 GL 纹理并重置尺寸字段

    uint32_t m_textureId = 0;  ///< GL 纹理句柄，0 表示无效
    int32_t m_width = 0;       ///< 加载后的宽
    int32_t m_height = 0;      ///< 加载后的高
    int32_t m_channels = 0;    ///< 加载后的通道数
};

} // namespace melody_matrix::renderer
