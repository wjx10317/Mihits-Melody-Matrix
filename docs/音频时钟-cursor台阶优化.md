# BGM 同步：cursor 台阶与 anchor 精度 — 修改评估

> 背景：Tap 判定 `dt = pressTimeMs - note.time` 中，`pressTimeMs` 来自 `Clock::songTimeAtTickMs(event.timestamp)`，即 **缓存 anchor + SDL 外推到按键时刻**（非 poll 时再读当前 cursor）。  
> 问题：anchor 绑定 **整数 ms cursor**，且仅在 `audioFrameTimeMs != prev` 时刷新；miniaudio `get_cursor` 可能 **~10ms 台阶**，导致 anchor 起点 δ 可达台阶宽度。  
> 结论：**优先实施方案一（PCM frame anchor）**；仍不足时再上 **方案二（跳变软对齐）**。

---

## 1. 当前实现

### 1.1 数据流（含按键）

```
Kernel::pumpInputEvents()
  └─ dispatchGameplayKeyEvent()
       ├─ expandSdlEventTimestamp(event.timestamp) → eventTick
       └─ Clock::songTimeAtTickMs(eventTick)
            = anchorAudioMs + (eventTick - anchorTick) + userOffset
            └─ PlayingState::handleKeyEvent → JudgeQueue::onKeyPress
                 └─ dt = pressTimeMs - note.time

PlayingState::syncClockFromAudio() / Kernel::syncPlayingClock()
  └─ m_audio.positionMs()
       └─ ma_sound_get_cursor_in_pcm_frames → 四舍五入为整数 ms
            └─ Clock::syncFromAudio(ms)
                 └─ 仅 ms 变化时刷新 anchorAudioMs / anchorTickMs

PlayingState::update() / 渲染
  └─ Clock::interpolatedNowMs()
       = anchorAudioMs + (SDL_GetTicks64() - anchorTick) + userOffset
```

**要点**：按键与 `interpolatedNowMs()` **共用 anchor**，但按键用 **eventTick**，逻辑步用 **当前 tick**；二者均 **不在 poll 时读 positionMs()**。

### 1.2 相关代码位置

| 文件 | 职责 |
|------|------|
| `src/audio/audio_engine.cpp` | `positionMs()`：PCM frames → 整数 ms |
| `src/core/clock.cpp` | `syncFromAudio()`、`songTimeAtTickMs()`、`interpolatedNowMs()` |
| `src/core/kernel.cpp` | `dispatchGameplayKeyEvent()`、`syncPlayingClock()` |
| `src/core/states/playing_state.cpp` | `syncClockFromAudio()`、`handleKeyEvent()` |
| `src/gameplay/judge_queue.cpp` | `onKeyPress()`：`dt = pressTimeMs - note.time` |

### 1.3 误差来源（可忽略 vs 待修）

| 来源 | 量级 | 处理 |
|------|------|------|
| PCM → ms 四舍五入 | ~±0.5ms | **可忽略** |
| SDL tick 量化 | 1ms | 接受 |
| cursor 整数 ms + 稀疏刷新 anchor | **0～~10ms** δ | **本文档方案一/二** |
| 外推段 SDL ≈ 音频同速 | 稳态 ~0 | 现有设计合理，不改 |

---

## 2. 问题机制简述

| 现象 | 原因 |
|------|------|
| anchor 绑在 1000ms，真实可能 1000～1009ms | cursor 台阶 + 整数 ms |
| 外推本身不「攒 10ms 误差」 | SDL 与 BGM 同速时，δ 为 **常值偏差** 直到下次校正 |
| 240Hz poll 无法消除台阶 | `get_cursor` 可能连读相同 frames，与 poll 频率无关 |

---

## 3. 错误 / 不建议做法 — 禁止

| 做法 | 后果 |
|------|------|
| 每次 `syncFromAudio` 都重置 anchor（ms 未变也重置 `anchorTick`） | 插值被停滞 cursor 拽回，时钟 **落后 ~半个 cursor 周期**（`clock.cpp` 注释） |
| 按键改用 `interpolatedNowMs()`（poll 时刻） | 丢失 `event.timestamp`，晚处理时 **人为偏大** |
| 仅提高 poll / fixed update 到 1000Hz | cursor API 不变则无效，CPU 浪费 |
| 按键时现读 `positionMs()` 当作 pressTime | 得到 poll 时刻 cursor，**非按键瞬间** |

---

## 4. 方案一：PCM frame anchor（优先）

### 4.1 思路

- anchor 存 **PCM 帧** + `sampleRate`，不再用 **整数 ms 是否变化** 作为唯一刷新条件。
- **`cursorFrames != prevFrames` 即刷新 anchor**（240Hz sync 下，frames 常比 ms 更细）。
- `songTimeAtTickMs` / `interpolatedNowMs` 在 **帧域外推**，**最后再** 换算 ms 供判定。

```
anchorFrames + (tick - anchorTick) * sampleRate / 1000  →  ms（四舍五入）
```

### 4.2 需新增 / 修改的接口

**`audio_engine.h` / `.cpp`**

```cpp
struct AudioPosition {
    ma_uint64 frames = 0;
    ma_uint32 sampleRate = 48000;
};
AudioPosition positionFrames() const;  // 读 cursorFrames + sr
// positionMs() 可改为由 frames 导出，保持对外兼容
```

**`clock.h` / `.cpp`**

```cpp
void syncFromAudioFrames(ma_uint64 frames, ma_uint32 sampleRate);
// 内部：frames != prevFrames 时更新 m_anchorFrames / m_anchorTickMs / m_sampleRate
// songTimeAtTickMs / interpolatedNowMs：帧外推 → ms
```

**`playing_state.cpp`**

```cpp
void PlayingState::syncClockFromAudio() {
    auto pos = m_audio.positionFrames();
    kernel.clock().syncFromAudioFrames(pos.frames, pos.sampleRate);
}
```

### 4.3 改动量估算

| 文件 | 变更 |
|------|------|
| `src/audio/audio_engine.h` | +`AudioPosition`、`positionFrames()` |
| `src/audio/audio_engine.cpp` | +`positionFrames()`，`positionMs()` 可复用 frames |
| `src/core/clock.h` | +frame 成员、`syncFromAudioFrames()` |
| `src/core/clock.cpp` | 重写 anchor 刷新与 `songTimeAtTickMs` / `interpolatedNowMs` |
| `src/core/states/playing_state.cpp` | `syncClockFromAudio()` 改调 frames |
| `src/gameplay/judge_queue.cpp` | **0 行**（仍收 ms） |
| `src/core/kernel.cpp` | **0 行**（按键路径不变） |

合计约 **60～90 行**。

### 4.4 预期收益与风险

| 项 | 说明 |
|----|------|
| 预期收益 | Tap `dt` 的 anchor δ 从 **~10ms 量级** 降到 **~1～2ms（或 miniaudio 实际 frame 步长）** |
| 改坏概率 | **~15～25%**（seek/pause/resume 与 frame 锚定边界） |

### 4.5 与按键链的关系

- `dispatchGameplayKeyEvent` **仍只调** `songTimeAtTickMs(eventTick)`，**不改**。
- 改进的是 anchor 精度与刷新频率，**不是** 判定公式。

---

## 5. 方案二：cursor 跳变软对齐（进阶，依赖方案一）

### 5.1 思路

方案一实施后，若 miniaudio 仍 **一次跳多 ms**，硬写 anchor 可能与 **SDL 外推** 差数 ms，造成时钟 **可视抖动** 或 **dt 阶跃**。

在 **frames 变化** 时：

```cpp
const int64_t extrapolatedMs = framesToMs(anchorFrames + elapsedFrames);
const int64_t cursorMs       = framesToMs(newFrames);
const int64_t drift          = cursorMs - extrapolatedMs;

if (std::abs(drift) <= kSoftAlignMs) {   // 建议 2～3ms
    // 小漂移：保留外推连续性，anchor 用外推反算 frames
    m_anchorFrames = msToFrames(extrapolatedMs);
} else {
    // 大跳：seek / 卡顿 / 长时间失步，硬跟 cursor
    m_anchorFrames = newFrames;
}
m_anchorTickMs = SDL_GetTicks64();
```

### 5.2 改动量估算

| 文件 | 变更 |
|------|------|
| `src/core/clock.cpp` | `syncFromAudioFrames` 内 +15～25 行 |
| 其他 | **0 行**（若方案一已落地） |

### 5.3 预期收益与风险

| 项 | 说明 |
|----|------|
| 预期收益 | 台阶跳变时 **减少 anchor 硬切**；高 OD Perfect 边缘更稳 |
| 改坏概率 | **~10～15%**（`kSoftAlignMs` 需实测；过大则贴近旧行为，过小则软对齐无效） |
| 依赖 | **必须在方案一之后**；单独做意义不大 |

### 5.4 可选扩展（非必须）

- **双样本线性插值**：存最近两次 `(frames, tick)`，按键时在样本间插值。仅当方案一+二仍不足时考虑（复杂度高，见会话讨论方案 C）。

---

## 6. 收益评估（综合）

| 项 | 方案一 | 方案一 + 二 |
|----|--------|-------------|
| Tap dt anchor δ | ~1～2ms 典型 | 跳变瞬间更平滑 |
| 工程风险 | 低～中 | 中 |
| JudgeQueue / 按键调用栈 | 不变 | 不变 |

**综合：方案一 ROI 高，建议先做；方案二按 A/B 实测再定。**

---

## 7. 若仍要实施：推荐步骤

### 7.1 方案一

1. 实现 `positionFrames()` + `syncFromAudioFrames()`。
2. 保留 `syncFromAudio(int64_t ms)` 为薄封装（可选，便于 Debug HUD）。
3. 单元 / 手动测试：
   - [ ] 正常开局 → 首 note Tap，`dt` 分布（Debug HUD / 日志）
   - [ ] Skip 前导（空格 seek）
   - [ ] 暂停 → 恢复
   - [ ] Retry / 重进 PlayingState
   - [ ] 高 OD（8～10）Perfect 边缘手感
   - [ ] `songTimeAtTickMs(event)` vs `interpolatedNowMs()` 同 tick 一致性

### 7.2 方案二（方案一通过后）

1. 增加 `kSoftAlignMs`（建议先 3，config 可配）。
2. A/B：跳变附近 `dt` 是否仍有 **~5ms 阶跃**。
3. 与 `timing_offset_ms` 回归测试。

---

## 8. 决策记录

| 日期 | 结论 |
|------|------|
| 2026-07-09 | 文档化；**待实施**方案一（PCM frame anchor）；方案二备查 |
