/**
 * @file asset_loader.cpp
 * @brief AssetLoader 实现
 *
 * 文件职责：
 *   管理后台加载线程的生命周期与原子进度标志。
 *
 * 主要依赖：
 *   asset_loader.h、util/logger.h。
 *
 * 在项目中的用法：
 *   由 BootState 等启动流程使用，主线程不直接 include 本文件。
 */
#include "asset_loader.h"  // AssetLoader 类声明
#include "util/logger.h"   // MM_LOG_INFO：记录加载起止日志

namespace melody_matrix::core {

AssetLoader::~AssetLoader() {
    // 若线程仍在运行，阻塞等待其结束，避免 use-after-free
    if (m_thread.joinable()) {
        m_thread.join();  // 等待后台任务完成
    }
}

void AssetLoader::setTask(LoadFunc task) {
    m_task = std::move(task);  // 转移所有权，须在 start() 前设置
}

void AssetLoader::start() {
    // exchange(true) 返回旧值；若已为 true 则说明已启动，防止重复
    if (m_started.exchange(true)) return;  // 已启动则直接返回

    // 启动后台线程执行 m_task，完成后置 progress=1 且 done=true
    m_thread = std::thread([this]() {
        MM_LOG_INFO("AssetLoader", "Background loading started");
        if (m_task) {
            m_task(*this);  // 执行用户注册的加载逻辑，期间可调用 setProgress
        }
        // 无任务或任务完成后标记 100% 并 done
        m_progress.store(1.0f);  // 进度拉满
        m_done.store(true);       // 通知主线程加载结束
        MM_LOG_INFO("AssetLoader", "Background loading complete");
    });
}

void AssetLoader::wait() {
    // 主线程显式等待加载完成（Boot 等流程可能调用）
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AssetLoader::setProgress(float p) {
    m_progress.store(p);  // 加载任务内更新进度，供 UI 进度条读取
}

float AssetLoader::progress() const {
    return m_progress.load();  // 主线程无锁读取当前进度
}

bool AssetLoader::done() const {
    return m_done.load();  // 主线程轮询是否可进入下一状态
}

} // namespace melody_matrix::core
