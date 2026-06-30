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
#include "asset_loader.h"
#include "util/logger.h"

namespace melody_matrix::core {

AssetLoader::~AssetLoader() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AssetLoader::setTask(LoadFunc task) {
    m_task = std::move(task);
}

void AssetLoader::start() {
    // exchange(true) 返回旧值；若已为 true 则说明已启动，防止重复
    if (m_started.exchange(true)) return;

    m_thread = std::thread([this]() {
        MM_LOG_INFO("AssetLoader", "Background loading started");
        if (m_task) {
            m_task(*this);
        }
        // 无任务或任务完成后标记 100% 并 done
        m_progress.store(1.0f);
        m_done.store(true);
        MM_LOG_INFO("AssetLoader", "Background loading complete");
    });
}

void AssetLoader::wait() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AssetLoader::setProgress(float p) {
    m_progress.store(p);
}

float AssetLoader::progress() const {
    return m_progress.load();
}

bool AssetLoader::done() const {
    return m_done.load();
}

} // namespace melody_matrix::core
