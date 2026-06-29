#pragma once

#include <string>
#include <cstdint>

namespace melody_matrix::util {

/// 计算 SHA-256 哈希值。
/// @param data 输入数据指针
/// @param size 数据长度（字节）
/// @return 64 字符的十六进制哈希字符串
std::string sha256(const void* data, size_t size);

} // namespace melody_matrix::util
