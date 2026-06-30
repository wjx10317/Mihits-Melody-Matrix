/**
 * @file hash.cpp
 * @brief SHA-256 哈希算法实现（FIPS 180-4）
 *
 * 文件职责：
 *   实现 hash.h 声明的 sha256()，内含完整的 SHA-256 压缩与填充逻辑。
 *
 * 主要依赖：
 *   标准库 <sstream>、<iomanip>、<cstring>；无 OpenSSL 等外部依赖。
 *
 * 在项目中的用法：
 *   由 hash.h 间接引用，业务代码不直接 include 本文件。
 */
#include "hash.h"

#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdint>

namespace melody_matrix::util {

// ──────────────────────────────────────────────────────
//  最小化 SHA-256 实现（FIPS 180-4），约 150 行，零外部依赖
// ──────────────────────────────────────────────────────

namespace {

// SHA-256 轮常量 K（前 64 个质数立方根小数部分的前 32 位）
static constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

/** @brief 32 位循环右移 */
inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
/** @brief SHA 选择函数 Ch */
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z)  { return (x & y) ^ (~x & z); }
/** @brief SHA 多数函数 Maj */
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
/** @brief 大端 Σ0 变换 */
inline uint32_t ep0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
/** @brief 大端 Σ1 变换 */
inline uint32_t ep1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
/** @brief 消息扩展 σ0 */
inline uint32_t sig0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
/** @brief 消息扩展 σ1 */
inline uint32_t sig1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

/** @brief SHA-256 增量哈希上下文，保存中间状态与缓冲块 */
struct Sha256Ctx {
    uint32_t state[8];   ///< 8 个 32 位工作变量（a~h）
    uint8_t  buffer[64]; ///< 未满 512 位的待处理字节
    uint64_t bitCount;   ///< 已输入的总比特数
};

/**
 * @brief 初始化上下文为 FIPS 180-4 规定的初始哈希值
 * @param ctx 待初始化的上下文
 */
void sha256Init(Sha256Ctx& ctx) {
    ctx.state[0] = 0x6a09e667; ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372; ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f; ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab; ctx.state[7] = 0x5be0cd19;
    ctx.bitCount = 0;
    std::memset(ctx.buffer, 0, 64);
}

/**
 * @brief 对单个 512 位（64 字节）块执行压缩轮
 * @param state 当前哈希状态（输入/输出）
 * @param block 512 位消息块
 */
void sha256Transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    // 将 64 字节块按大端序展开为 W[0..15]
    for (int i = 0; i < 16; ++i) {
        W[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
               (uint32_t(block[i * 4 + 2]) << 8)  |  uint32_t(block[i * 4 + 3]);
    }
    // 消息扩展：W[16..63]
    for (int i = 16; i < 64; ++i) {
        W[i] = sig1(W[i - 2]) + W[i - 7] + sig0(W[i - 15]) + W[i - 16];
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    // 64 轮主循环
    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + ep1(e) + ch(e, f, g) + K[i] + W[i];
        uint32_t t2 = ep0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/**
 * @brief 增量喂入数据，满 64 字节时触发一次压缩
 * @param ctx 哈希上下文
 * @param data 输入字节流
 * @param len 字节数
 */
void sha256Update(Sha256Ctx& ctx, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx.buffer[ctx.bitCount / 8 % 64] = data[i];
        ctx.bitCount += 8;
        if ((ctx.bitCount / 8) % 64 == 0) {
            sha256Transform(ctx.state, ctx.buffer);
        }
    }
}

/**
 * @brief 完成填充并输出最终 256 位摘要
 * @param ctx 哈希上下文
 * @param hash 输出缓冲区（至少 32 字节）
 */
void sha256Final(Sha256Ctx& ctx, uint8_t hash[32]) {
    uint64_t bitLen = ctx.bitCount;
    uint8_t pad = 0x80;
    sha256Update(ctx, &pad, 1);

    // 填充零直到长度 ≡ 448 mod 512（即字节长度 ≡ 56 mod 64）
    while ((ctx.bitCount / 8) % 64 != 56) {
        pad = 0;
        sha256Update(ctx, &pad, 1);
    }

    // 追加原始消息长度（大端 64 位）
    uint8_t lenBytes[8];
    for (int i = 7; i >= 0; --i) {
        lenBytes[i] = static_cast<uint8_t>(bitLen & 0xFF);
        bitLen >>= 8;
    }
    sha256Update(ctx, lenBytes, 8);

    // 将 state 按大端序写入 32 字节摘要
    for (int i = 0; i < 8; ++i) {
        hash[i * 4]     = static_cast<uint8_t>((ctx.state[i] >> 24) & 0xFF);
        hash[i * 4 + 1] = static_cast<uint8_t>((ctx.state[i] >> 16) & 0xFF);
        hash[i * 4 + 2] = static_cast<uint8_t>((ctx.state[i] >> 8) & 0xFF);
        hash[i * 4 + 3] = static_cast<uint8_t>(ctx.state[i] & 0xFF);
    }
}

/**
 * @brief 将二进制数据转为小写十六进制字符串
 * @param data 输入字节
 * @param len 字节数
 * @return 十六进制字符串
 */
std::string toHex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

} // anonymous namespace

std::string sha256(const void* data, size_t size) {
    Sha256Ctx ctx;
    sha256Init(ctx);
    sha256Update(ctx, static_cast<const uint8_t*>(data), size);

    uint8_t hash[32];
    sha256Final(ctx, hash);
    return toHex(hash, 32);
}

} // namespace melody_matrix::util
