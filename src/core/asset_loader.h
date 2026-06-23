#pragma once

#include <atomic>
#include <thread>
#include <functional>

namespace melody_matrix::core {

/// 异步资源加载器，在后台线程执行加载任务。
/// 提供进度查询接口，供主线程驱动加载进度条。
class AssetLoader {
public:
    /// 加载任务函数签名
    using LoadFunc = std::function<void(class AssetLoader& loader)>;

    AssetLoader() = default;
    ~AssetLoader();

    /// 设置加载任务（start 前调用）
    void setTask(LoadFunc task);

    /// 启动后台线程执行加载任务
    void start();

    /// 阻塞等待加载完成
    void wait();

    /// 更新进度（0.0 ~ 1.0），由加载任务调用
    void setProgress(float p);

    /// 获取当前进度
    float progress() const;

    /// 加载是否完成
    bool done() const;

private:
    LoadFunc m_task;
    std::thread m_thread;
    std::atomic<float> m_progress{0.0f};
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_started{false};
};

} // namespace melody_matrix::core
