/**
 * @file hash.h
 * @brief SHA-256 哈希计算接口
 *
 * 文件职责：
 *   提供对任意二进制数据计算 SHA-256 摘要的公共 API，返回十六进制字符串。
 *
 * 主要依赖：
 *   标准库 <string>、<cstdint>；实现在 hash.cpp 中（无第三方加密库）。
 *
 * 在项目中的用法：
 *   被谱面导入、资源校验等模块调用，用于比对文件内容或生成唯一标识。
 *   调用方只需 `#include "util/hash.h"` 并调用 sha256()。
 */
#pragma once

#include <string>
#include <cstdint>

namespace melody_matrix::util {

/**
 * @brief 计算 SHA-256 哈希值
 * @param data 输入数据指针
 * @param size 数据长度（字节）
 * @return 64 字符的小写十六进制哈希字符串
 */
std::string sha256(const void* data, size_t size);

} // namespace melody_matrix::util
