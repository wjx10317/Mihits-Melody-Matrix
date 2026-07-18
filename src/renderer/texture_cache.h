#pragma once

// ============================================================
// texture_cache.h — 全局纹理缓存（单例）
// 路径 → Texture2D 缓存；后台线程 stbi 解码，主线程 glTexImage2D 上传。
// ============================================================

#include "renderer/texture.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace melody_matrix::renderer {

/// 全局纹理缓存，生命周期贯穿整个应用。
class TextureCache {
public:
    static TextureCache& instance();

    /// 同步加载（decode + upload 均在主线程；init 等必须立即可用的路径）
    Texture2D* loadSync(const std::string& path, bool genMipmap = false);

    /// 异步请求：后台 decode，主线程 processPendingUploads 上传；未就绪时返回 nullptr
    Texture2D* requestLoad(const std::string& path, bool genMipmap = false);

    /// 兼容旧 API：等同 requestLoad（异步）
    Texture2D* load(const std::string& path, bool genMipmap = false) {
        return requestLoad(path, genMipmap);
    }

    /// 主线程每帧调用：将 decode 完成的像素上传为 GL 纹理（默认每帧最多 8 张）
    void processPendingUploads(int maxUploads = 8);

    Texture2D* get(const std::string& path);
    void unload(const std::string& path);

    void preload(const std::vector<std::string>& paths);
    void preloadRange(const std::vector<std::string>& paths, int start, int end);
    /// 仅卸载 paths 中落在 [center±radius] 之外的纹理；不影响 cache 内其它条目（如 note 皮肤）。
    void unloadDistant(const std::vector<std::string>& paths, int centerIndex, int radius);

    /// 已在缓存且 valid
    bool isLoaded(const std::string& path) const;

    /// decode 或 upload 失败
    bool hasFailed(const std::string& path) const;

    /// 仍在 decode 或 upload 队列中
    bool isPending(const std::string& path) const;

    void clear();
    void shutdown();
    size_t size() const;

private:
    TextureCache();
    ~TextureCache();

    struct DecodeRequest {
        std::string path;
        bool genMipmap = false;
    };

    struct DecodedImage {
        std::string path;
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        int channels = 0;
        bool genMipmap = false;
    };

    void ensureDecodeThread();
    void decodeWorkerLoop();
    static std::string normalizeKey(const std::string& path);
    void revokePath(const std::string& key);

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Texture2D> m_cache;
    std::unordered_set<std::string> m_failedPaths;
    std::unordered_set<std::string> m_inFlightPaths;
    std::unordered_set<std::string> m_revokedPaths; ///< unload 后丢弃 decode/upload 结果

    std::deque<DecodeRequest> m_decodeQueue;
    std::deque<DecodedImage> m_uploadQueue;

    std::thread m_decodeThread;
    std::condition_variable m_decodeCv;
    std::atomic<bool> m_stopDecode{false};
    std::atomic<bool> m_threadStarted{false};
};

} // namespace melody_matrix::renderer
