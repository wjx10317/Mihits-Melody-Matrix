# 音频时序改造总计划（HostClock / AudioPlayhead）

> 本文档覆盖并取代原「BGM 同步：cursor 台阶与 anchor 精度」主叙事。  
> 分支：`test`（大改在此进行；`main` 保留 Stable 判定基线）。  
> 本轮目标：**方案 B** — 文档先行 + `HostClock` 接入 `core::Clock`；**不接 WASAPI**。  
> 源码级延迟审查全文（Canvas 同步副本）：[`音画判定延迟审查.md`](./音画判定延迟审查.md)。

---

## 1. 目标与非目标

### 1.1 目标

| 层级 | 内容 |
|------|------|
| 文档 | 固化跨平台时钟架构与里程碑，避免旧「PCM frame anchor」结论占满上下文 |
| 主机钟 | `melody_matrix::time::HostClock`：跨平台单调高精度时刻（Win = QPC） |
| 听感位 | `time::AudioPlayhead`：设备/缓冲播放位置接口（本轮仅预留） |
| 歌曲钟 | `core::Clock` 仍为权威歌曲时间；外推改用 HostClock，与按键同时间域 |
| 耦合 | 本轮只动文档 + `src/time/*` + `clock.*` + `kernel` 按键一处；不改 PlayingState / AudioEngine / 判定 |

### 1.2 非目标（输入里程碑仍不做）

- 应用内自动校准到 ±2～3ms
- C3 亚毫秒内部时间域（见 §6.1）
- `WH_KEYBOARD_LL` / 手柄判定路径
- 改判定窗口、miniaudio 播放/混音路径
- 把 QPC / WASAPI / Raw 泄漏进 `JudgeQueue`、UI、谱面解析

---

## 2. 现状数据流

```
Kernel::pumpInputEvents()
  └─ active IGameplayInput（二选一，初始化时选定）
       ├─ RawInputAdapter（Win，优先）← WM_INPUT 收包瞬间 HostClock
       └─ 回退 SdlPollInputAdapter ← SDL_PollEvent 处理瞬间 HostClock
            └─ dispatchGameplayKey(key, pressed, hostMs)
                 └─ Clock::songTimeAtHostMs(hostMs)
                      = anchorAudioMs + (hostMs - anchorHostMs) + userOffset
                      └─ PlayingState::handleKeyEvent → JudgeQueue
                           └─ dt = pressTimeMs - note.time

PlayingState::syncClockFromAudio() / Kernel::syncPlayingClock()
  └─ IAudioPlayhead::positionMs()
       ├─ C2: WasapiPlayhead ← IAudioClock（听感设备位置 + 歌曲 epoch）
       └─ 回退: MiniaudioCursorPlayhead ← queryPlaybackCursor
            └─ Clock::syncFromAudio(ms)
                 └─ 前进时刷新 anchorAudioMs / anchorHostMs

PlayingState::update() / 渲染
  └─ Clock::interpolatedNowMs()
       = anchorAudioMs + (HostClock 外推) + userOffset
```

**要点**

- 歌曲时间经 **AudioPlayhead** 同步；C1 实现仍是 write cursor，不是喇叭真实发声位置。
- 按键与插值共用 HostClock 域；判定输入为 Raw / SDL 两适配器，Raw 初始化失败回退 SDL（互斥，非双开去重）。
- miniaudio 继续负责解码、混音、播放；C2 用 WASAPI 设备钟替换 Playhead 实现。

---

## 3. 问题分层

| 层 | 现象 | 根因 | 本轮是否解决 |
|----|------|------|----------------|
| A. 主机时间域 | SDL 毫秒戳与高精度钟混用会算错 dt | 平台 API 泄漏进业务 | **方案 B 纠错**：Clock 与按键统一 HostClock |
| B. write cursor ≠ 听感 | 判定偏早/偏晚数～数十 ms | cursor 是缓冲位置，非 DAC | C1 已收口到 Playhead；听感真理 → **C2 WASAPI** |
| C. 输入戳精度 | 处理时刻 ≠ 按键瞬间 | SDL poll + Host 戳 | 输入里程碑：Raw 适配器 + 失败回退 SDL |
| D. cursor 台阶 | anchor δ 可达 ~10ms | 整数 ms + 稀疏刷新 | 降级为可选优化，非主路径 |

---

## 4. 模块边界（减少耦合）

```
┌─────────────────────────────────────────────────────────┐
│  JudgeQueue / PlayingState / UI                         │
│  只认 int64_t 歌曲 ms；不 include Windows / WASAPI      │
└───────────────────────────┬─────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────┐
│  core::Clock（歌曲钟）                                    │
│  syncFromAudio(ms) ← 锚点来自音频层                      │
│  外推 / songTimeAtHostMs ← 只用 time::HostClock          │
└─────────────┬───────────────────────────┬───────────────┘
              │                           │
┌─────────────▼──────────┐   ┌────────────▼────────────────┐
│ time::HostClock        │   │ time::IAudioPlayhead         │
│ nowMs() / nowNs()      │   │ positionFrames / Ms          │
│ Win: QPC               │   │ C1: MiniaudioCursorPlayhead  │
│ 其它: steady_clock     │   │ C2: WasapiPlayhead（IAudioClock）│
└────────────────────────┘   └────────────┬─────────────────┘
                                          │
                             ┌────────────▼────────────────┐
                             │ audio::AudioEngine            │
                             │ 播放 / queryPlaybackCursor   │
                             └─────────────────────────────┘
```

| 名称 | 命名空间 | 职责 | 禁止 |
|------|----------|------|------|
| **HostClock** | `melody_matrix::time` | 主机单调高精度时刻 | 头文件不 `#include <windows.h>`；不进判定逻辑 |
| **IAudioPlayhead** | `melody_matrix::time` | 「播到哪了」的抽象 | 实现细节不进 JudgeQueue |
| **MiniaudioCursorPlayhead** | `melody_matrix::audio` | C1 write-cursor 适配 | 勿当作听感真理 |
| **Clock** | `melody_matrix::core` | 歌曲时间权威 | 不直接调 QPC / WASAPI |
| **AudioEngine** | `melody_matrix::audio` | 播放引擎 | 不承担最终听感校准 |

**miniaudio 仍有用**：播 BGM/SFX、解码、混音、跨平台设备。  
WASAPI 只补「设备时钟」；二者不是二选一。

---

## 5. 方案 B 自检纠错（必读）

若 Clock 用 HostClock 外推，而按键仍传 `SDL_Event.timestamp`：

→ **两个时间域相减** → `songTimeAtHostMs` 错误 → 判定崩溃式偏移。

**纠错后的方案 B（已落地）**

1. `Clock` 锚点 **只存 HostClock ms**（`m_anchorHostMs`）；**已删除** SDL_GetTicks64 / `expandSdlEventTimestamp` 判定路径。
2. `Kernel::dispatchGameplayKeyEvent` 传入 `HostClock::nowMs()`（本轮用**处理时刻**；日后改为按键瞬间戳）。
3. API 更名为 `songTimeAtHostMs`；不再保留「SDL tick」命名或备用实现。
4. C1：`PlayingState` 经 `MiniaudioCursorPlayhead` → `syncFromAudio`；C2 再换 WASAPI 实现。

---

## 6. 里程碑

| 阶段 | 内容 | 状态 |
|------|------|------|
| **0 文档** | 本文覆盖旧 cursor 文档 | **已落地** |
| **B 主机钟** | `src/time` HostClock + Clock 外推 + Kernel 同域按键戳；删除 SDL 判定时钟 | **已落地** |
| **C1 Playhead** | `MiniaudioCursorPlayhead` + Playing 经 Playhead 同步 | **已落地** |
| **C2 WASAPI** | `WasapiPlayhead`（IAudioClock2/QPC 外推）+ cursor 回退 + 逐帧 song-ph 窗口统计 | **已落地** |
| **C3 亚毫秒** | 内部帧/ns 时间域；日志/UI 再转 ms（消除整数 ms 网格残差） | **计划封存（近期不实施）** |
| **输入** | Raw / SDL 双适配器；优先 Raw（按键瞬间 Host），init 失败回退 SDL | **已落地** |
| **校准** | 短窗估计固定延迟 → 自动 offset | 未开始 |

#### 输入适配器约定

- `IGameplayInput`：`RawInputAdapter` 与 `SdlPollInputAdapter` 可替换；进程内 Playing 判定只挂接一个。
- 初始化：`try Raw.init` → 失败则 `SdlPoll`；非 Win 直接 SDL。日志 `backend=raw|sdl`。
- Raw：在 HWND 子类化回调里**同步** `GetRawInputData`（不可经 `SDL_SYSWMEVENT` 延后），入队后由 `poll()` 交付；失败回退 SDL。
- 选定 Raw 时：SDL KEY 仍可供 ImGui/菜单，但不进判定；禁止双路径判定后再去重。
- UI/ESC 状态机仍走 SDL 事件泵。
- 自测：`verify_gameplay_input`（回退 / Raw 优选 / SendInput / 连发抑制）。

旧「方案一 PCM frame anchor / 方案二软对齐」→ **附录 A**；可作为 Playhead 精度优化的可选项，**不再作为主路径**。

### 6.1 C3 亚毫秒 — 计划封存（2026-07-18）

**决策：** 近期不实现。当前整数 ms + WASAPI/QPC 外推残差约 ±1ms，判定够用；C3 仅作远期备忘，不进入当前迭代排期。

**若将来解封，范围概要（勿提前动工）：**

| 项 | 内容 |
|----|------|
| 目标 | 去掉 `song-ph` 的整数 ms 网格残差；内部统一帧或 ns |
| 触点 | `Clock` / `IAudioPlayhead` / 按键→歌曲时间换算；判定队列内部 |
| 边界 | 日志与 HUD 仍可显示 ms；谱面文件时间仍为 ms |
| 非目标 | 不替代 Raw Input、自动校准、端到端回环验收 |

**解封条件（建议）：** 有线参考设备上，整数 ms 残差已被证明是达标瓶颈，且输入/校准路径已就绪或并行推进。

---

## 7. 方案 B 改动清单与验收

### 7.1 允许改动的文件

| 路径 | 动作 |
|------|------|
| `docs/音频时钟-cursor台阶优化.md` | 本文件（覆盖） |
| `src/time/host_clock.h` | 新增 |
| `src/time/host_clock_win.cpp` | 新增（QPC） |
| `src/time/host_clock_stub.cpp` | 新增（非 Win） |
| `src/time/audio_playhead.h` | 新增（接口预留） |
| `src/time/audio_playhead_stub.cpp` | 新增（不被调用） |
| `src/core/clock.h` / `clock.cpp` | 外推改 HostClock；注释改时间域说明 |
| `src/core/kernel.cpp` | 按键改传 `HostClock::nowMs()` |

### 7.2 禁止改动（本轮）

`playing_state.*`、`audio_engine.*`、`judge_*`、`score_*`、`hp_*`、UI、谱面解析。

### 7.3 验收标准

- [ ] 工程编译通过（Win 走 QPC；其它平台 stub 可链）
- [ ] 进游玩可判定；Autoplay 正常；暂停/恢复/reset 时钟不飞
- [ ] `git diff` 文件集 ⊆ §7.1
- [ ] 业务代码无直接 `QueryPerformanceCounter` / WASAPI 调用

---

## 8. 禁止项（沿用并扩展）

| 做法 | 后果 |
|------|------|
| cursor 未前进时每次 `syncFromAudio` 都重置 anchorTick | 插值被停滞 cursor 拽回，时钟落后约半个周期 |
| 按键改用 `interpolatedNowMs()`（poll 时刻）冒充按键瞬间 | 晚处理时 dt 人为偏大 |
| 按键时现读 `positionMs()` 当 pressTime | 得到 poll 时刻 cursor，非按键瞬间 |
| 业务层直接写 QPC / WASAPI | 多平台与模块边界崩溃 |
| 仅提高 poll 到 1000Hz 指望消除 cursor 台阶 | cursor API 不变则无效 |

---

## 9. 决策记录

| 日期 | 结论 |
|------|------|
| 2026-07-09 | 旧文档：优先 PCM frame anchor（已降级，见附录） |
| 2026-07-17 | 判定改为 Stable 300/100/50，基线提交 `main`；大改在 `test` |
| 2026-07-17 | **主路径改为 HostClock + AudioPlayhead**；本轮先文档，再方案 B；WASAPI/校准后续 |
| 2026-07-18 | C2：WASAPI + QPC 外推落地；`song-ph` 逐帧窗口统计验证整数 ms 残差 |
| 2026-07-18 | **C3 亚毫秒计划封存**：近期不实施；见 §6.1 |
| 2026-07-18 | **输入**：Raw 与 SDL_PollEvent 为两适配器；Raw init 失败回退 SDL（同 playhead 回退模型） |

---

## 附录 A — 历史：cursor 台阶与 PCM frame anchor（降级备查）

原问题：anchor 绑整数 ms cursor，miniaudio `get_cursor` 可能 ~10ms 台阶，导致 anchor 起点 δ 偏大。

原「方案一」：`positionFrames()` + `syncFromAudioFrames`，帧域刷新与外推。  
原「方案二」：cursor 跳变时软对齐，减少硬切抖动。

**现评价**：在 write-cursor 体系内可减小台阶误差，但**不能**解决听感位置与自动校准；且易与 HostClock 改造抢主叙事。  
若 HostClock + WASAPI Playhead 落地后 cursor 台阶仍明显，可再评估是否在 Playhead 的 miniaudio 适配层做帧域优化。
