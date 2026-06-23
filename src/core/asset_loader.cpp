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
    if (m_started.exchange(true)) return; // 防止重复启动,将新值写入变量(原子操作)，返回旧值，如果旧值已经为true，说明已经开始了，直接return

    m_thread = std::thread([this]() {
        MM_LOG_INFO("AssetLoader", "Background loading started");
        if (m_task)//如果任务存在，(防止使用空指针)调用
        {
            m_task(*this);
        }//不存在进度直接为100%，返回已完成
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
    m_progress.store(p);//原子设值
}

float AssetLoader::progress() const {
    return m_progress.load();//原子读取
}

bool AssetLoader::done() const {
    return m_done.load();//原子读取
}

} // namespace melody_matrix::core
