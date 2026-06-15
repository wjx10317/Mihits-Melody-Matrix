#pragma once

#include <string>
#include <cstdint>

namespace melody_matrix::util {

/// 计算 SHA-256 哈希值。
/// @param data 输入数据指针
/// @param size 数据长度（字节）
/// @return 64 字符的十六进制哈希字符串
std::string sha256(const void* data, size_t size);

/// 计算文件内容的 SHA-256 哈希值。
/// @param filePath 文件路径
/// @return 64 字符的十六进制哈希字符串，失败返回空字符串
std::string sha256File(const std::string& filePath);

} // namespace melody_matrix::util
