/**
 * @file asset_loader.h
 * @brief 异步后台资源加载器
 *
 * 文件职责：
 *   在独立线程执行加载任务，主线程通过原子 progress/done 驱动进度条。
 *
 * 主要依赖：
 *   标准库 <thread>、<atomic>、<functional>。
 *
 * 在项目中的用法：
 *   BootState 等设置 LoadFunc 后 start()，主循环轮询 progress()/done() 更新 UI。
 */
#pragma once

#include <atomic>
#include <thread>
#include <functional>

namespace melody_matrix::core {

/**
 * @brief 异步资源加载器
 *
 * 加载逻辑在后台 std::thread 运行；任务内通过 setProgress() 报告进度。
 * 析构时 join 线程，确保生命周期安全。
 */
class AssetLoader {
public:
    /** @brief 加载任务函数：接收 AssetLoader 引用以便更新进度 */
    using LoadFunc = std::function<void(class AssetLoader& loader)>;

    AssetLoader() = default;
    ~AssetLoader();

    /**
     * @brief 设置加载任务（须在 start 前调用）
     * @param task 后台线程执行的函数
     */
    void setTask(LoadFunc task);

    /** @brief 启动后台线程（重复调用无效） */
    void start();

    /** @brief 阻塞等待加载线程结束 */
    void wait();

    /**
     * @brief 由加载任务调用，更新进度
     * @param p 进度 0.0 ~ 1.0
     */
    void setProgress(float p);

    /**
     * @brief 获取当前进度
     * @return 0.0 ~ 1.0
     */
    float progress() const;

    /**
     * @brief 加载是否完成
     * @return 线程任务结束且 progress 已为 1.0 时 true
     */
    bool done() const;

private:
    LoadFunc m_task;
    std::thread m_thread;
    std::atomic<float> m_progress{0.0f};
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_started{false};
};

} // namespace melody_matrix::core
