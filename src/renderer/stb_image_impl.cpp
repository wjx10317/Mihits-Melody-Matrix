// ============================================================
// stb_image_impl.cpp — stb_image 单例实现
// 全项目仅在此文件定义 STB_IMAGE_IMPLEMENTATION，避免重复链接。
// Texture2D / TextureCache 通过 #include <stb_image.h> 调用加载接口。
// ============================================================

#define STB_IMAGE_IMPLEMENTATION  // 启用 stb_image 函数体定义（仅本翻译单元一次）
#include <stb_image.h>            // 图像解码库头文件
