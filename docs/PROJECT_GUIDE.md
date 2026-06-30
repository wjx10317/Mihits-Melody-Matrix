# Melody Matrix 项目说明

> 面向入门阅读的完整指南：项目是什么、怎么构建、代码怎么组织、关键功能如何实现。

---

## 目录

1. [项目介绍](#1-项目介绍)
2. [快速开始](#2-快速开始)
3. [目录结构](#3-目录结构)
4. [架构总览](#4-架构总览)
5. [功能与实现对照](#5-功能与实现对照)
6. [渲染系统（OpenGL）](#6-渲染系统opengl)
7. [谱面与转换管线](#7-谱面与转换管线)
8. [判定与游玩循环](#8-判定与游玩循环)
9. [音频系统](#9-音频系统)
10. [CMake 构建说明](#10-cmake-构建说明)
11. [第三方库](#11-第三方库)
12. [配置与资源路径](#12-配置与资源路径)
13. [扩展阅读](#13-扩展阅读)
14. [源码中文注释说明](#14-源码中文注释说明)

---

## 1. 项目介绍

**Melody Matrix** 是一款 PC 端 **4K 节奏游戏**（类似 osu!mania 4K），使用 C++17 编写。

核心特点：

| 特点 | 说明 |
|------|------|
| **动态呼吸矩阵** | 谱面密度变化时，游玩网格行列数会随 Formation 变换（如 4×5 → 4×6） |
| **列滚动** | 列数超过 4 时，活跃判定窗为 4 列（D/F/J/K），其余列通过滚动进入窗口 |
| **双格式谱面** | 原生 `.mma` 格式；可导入 `.osu`（std）并转换为 `.mma` |
| **固定逻辑帧** | 逻辑 240 Hz，渲染跟随 VSync；判定时间对齐音频 |

技术栈：**SDL2**（窗口/输入）、**OpenGL 3.3 Core + GLAD**（渲染）、**Dear ImGui**（UI）、**miniaudio**（音频）、**GLM**（数学）。

---

## 2. 快速开始

### 2.1 依赖准备

第三方库放在 `third_party/`，可用脚本下载：

```bash
python scripts/download_deps.py
```

### 2.2 CMake 配置与编译（Windows + MSVC）

```powershell
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Debug
```

可执行文件输出：`build-vs/bin/Debug/melody_matrix.exe`

构建后会自动复制：

- `SDL2.dll`
- `assets/`（谱面、纹理等）
- `res/`（音效、UI 贴图）

### 2.3 运行

从 **exe 所在目录** 启动（`main.cpp` 会将工作目录设为 exe 目录），确保存在：

```
melody_matrix.exe
assets/beatmaps/...
res/
config.ini
logs/
```

---

## 3. 目录结构

```
Melody-Matrix/
├── CMakeLists.txt          # 顶层构建入口
├── cmake/
│   └── Dependencies.cmake  # 第三方库引入
├── src/                    # 全部业务源码（本文档重点）
│   ├── main.cpp            # 程序入口
│   ├── core/               # 内核、时钟、状态机、各游戏状态
│   ├── beatmap/            # 谱面解析、转换、滚动模拟
│   ├── gameplay/           # 判定队列、分数、HP、阵型控制
│   ├── renderer/           # OpenGL 渲染
│   ├── audio/              # miniaudio 封装
│   ├── platform/           # 配置、文件系统、对话框、zip
│   ├── ui/                 # ImGui 主题与管理
│   └── util/               # 日志、Result、事件总线
├── assets/                 # 运行时谱面与部分纹理
├── res/                    # 音效与 note 贴图
├── third_party/            #  vendored 依赖（勿改注释任务范围外）
├── docs/                   # 设计/说明文档
└── tests/                  # 单元测试（MM_BUILD_TESTS=ON）
```

---

## 4. 架构总览

### 4.1 启动流程

```
main()
  ├─ 工作目录 → exe 目录
  ├─ Logger::init
  ├─ Kernel::init()        // SDL 窗口 + GL 3.3 + Renderer + ImGui
  ├─ StateManager 注册 6 个状态
  ├─ transitionTo(Boot)
  └─ Kernel::run()           // 主循环（阻塞）
```

### 4.2 主循环（Kernel::run）

每一 **渲染帧**：

1. **syncPlayingClock** — 从 `AudioEngine::positionMs()` 同步歌曲时钟
2. **pumpInputEvents** — SDL 事件 → ImGui + 游戏按键（Playing 态立即判定）
3. **固定步长累积** — 以 240 Hz 调用 `StateManager::update(FIXED_DT)`
4. **OpenGL 清屏 + Renderer::renderFrame(visualTimeMs)**
5. **ImGui** — `StateManager::render()` 绘制 HUD/菜单
6. **SwapBuffers**

**visualTimeMs** = 插值歌曲时间 + 视觉前移（让 note 中心对齐击打时刻）。

### 4.3 状态机

```
Boot → MainMenu → SongSelect → Playing ⇄ Paused → Result
```

| 状态 | 文件 | 职责 |
|------|------|------|
| Boot | `boot_state.cpp` | 异步扫描谱面、预加载纹理、启动画面 |
| MainMenu | `main_menu_state.cpp` | 主菜单、设置、导入 .osz |
| SongSelect | `song_select_state.cpp` | 选曲、预览、难度 |
| Playing | `playing_state.cpp` | 核心游玩 |
| Paused | `paused_state.cpp` | 暂停 overlay |
| Result | `result_state.cpp` | 成绩展示 |

状态切换经 `SceneTransition` 做 200ms 黑场淡入淡出。

### 4.4 时间模型（重要）

```
AudioEngine.positionMs()
       ↓ syncFromAudio
Clock.interpolatedNowMs()
       ↓
PlayingState: judgeNow = songNow - timingOffsetMs   → JudgeQueue
Renderer:     visualNow = songNow + visualLead        → NoteRenderer
```

- **判定**跟音频，可配置 `timing_offset_ms` 补偿系统延迟
- **渲染**略超前，补偿 VSync 与显示延迟

---

## 5. 功能与实现对照

### 5.1 谱面加载

| 功能 | 实现位置 |
|------|----------|
| 按扩展名选解析器 | `beatmap_parser.h` → `createParserForFile()` |
| 解析 .mma | `mma_parser.cpp` |
| 解析 .osu | `osu_parser.cpp` |
| 校验并构建 Beatmap | `beatmap_builder.cpp` |
| 游玩态加载 | `PlayingState::initGameplay()` |

### 5.2 4 键映射与滚动窗

| 功能 | 实现位置 |
|------|----------|
| 活跃列窗口 | `PlayingState::ScrollWindow`（startCol/endCol） |
| 按键 → 列 | `PlayingState::getKeyMapping()` |
| 触发滚动 | `PlayingState::checkAndTriggerScroll()` |
| 滚动动画完成 | `PlayingState::completeScroll()` |
| 滚动公式（共用） | `beatmap/scroll_simulation.h` |

**滚动触发条件**：某列队首 note 进入 approach 窗口，且其列不在当前 4 列窗内 → 滚动到包含该 note 的窗口。

**Hold 阻塞**：任意列存在 active Hold 时不触发滚动（与 parser 策略一致）。

### 5.3 阵型变换（Formation）

| 功能 | 实现位置 |
|------|----------|
| 时间线插值 | `formation_controller.cpp` |
| 变换类型 | `beatmap.h` → `MatrixTransform`（SLIDE/ROTATE/SCALE 等） |
| 渲染侧动画 | `renderer.cpp` → `renderNotes()` / `beginFormationTransition()` |
| 判定锁定 | `PlayingState::isFormationJudgmentBlocked()` — 非 SCALE_ONLY 或行列变化时锁判定 |

### 5.4 判定

| 功能 | 实现位置 |
|------|----------|
| 分列队列 | `judge_queue.cpp` → `loadNotes()` |
| Perfect/Good/Miss 窗口 | `judge_strategy.h` → `StandardJudgeStrategy` |
| 按键按下 | `JudgeQueue::onKeyPress()` |
| Hold 头/尾 | `onKeyPress` + `onKeyRelease()` |
| Auto miss | `JudgeQueue::update()` |

OD 公式（与 osu 类似）：

- Perfect: ±(22 - 1.05×OD) ms
- Good: ±(65 - 2.6×OD) ms
- Miss: Good + 50 ms

### 5.5 分数 / Combo / HP

| 模块 | 文件 |
|------|------|
| 分数 | `score_manager.cpp` |
| 连击 | `combo_manager.cpp` |
| 血量 | `hp_manager.cpp` |

### 5.6 导入 osu 谱面

流程（`main_menu_state.cpp`）：

1. 用户选 `.osz` → `zip_extract.cpp` 解压
2. 选 `.osu` → `OsuParser::parse()` → `BeatmapBuilder::build()`
3. `MmaSerializer::writeToFile()` 写入 `assets/beatmaps/.../*.mma`
4. 复制 `audio.mp3`、`background.jpg/png`

---

## 6. 渲染系统（OpenGL）

### 6.1 渲染管线概览

```
Renderer::renderFrame(interpolatedTimeMs)
  ├─ 全屏谱面背景（bgShader + 纹理）
  ├─ 可选 dim 遮罩
  └─ NoteRenderer::render()   // 网格 + note + 缩圈 + Hold 光圈
```

ImGui 在 OpenGL 绘制 **之后** 叠加（HUD、菜单）。

### 6.2 固定格网（GridLayout）

文件：`renderer/grid_layout.h`

- 逻辑格固定 **256×256** 像素（与矩阵行列数无关）
- 矩阵总宽高 = `cols × 256` × `rows × 256`
- **活跃 4 列**水平居中；滚动时 `scrollOffset` 平移 X
- `blockSize`（来自 Formation）只缩放 **格内内容**，不缩放逻辑格

### 6.3 NoteRenderer：Instancing 四边形

文件：`renderer/note_renderer.cpp`

**思路**：一个单位 quad（两个三角形），用 **实例化属性** 批量绘制所有格子/note。

每个实例携带：

| 属性 | 含义 |
|------|------|
| `aInstance.xy` | 屏幕位置 |
| `aInstance.zw` | 宽高 |
| `aColor` | RGBA |
| `aTexLayer` | 纹理层索引（shader 内 switch） |
| `aArcSweep` | Hold 光圈角度 / 缩圈 / 击中扩散 |

**纹理层约定**（见 `note_renderer.h`）：

| Layer | 内容 |
|-------|------|
| 0 | Tap 本体 `tap.png` |
| 1 | Hold 本体 `slider.png` |
| 2 | 缩圈/击中扩散 `overlay.png` |
| 3 | Hold 进度环 `holdpush_ring.png` |
| 5 | 格子背景 `background.png` |
| 6–16 | Hold 进度填充 0%–100% |

### 6.4 Shader 要点

**顶点着色器**：`pos = aPos * size + offset`，传递 layer 与 arcSweep。

**片元着色器**：按 `layer` 采样对应纹理；layer 2 的 overlay 用 UV 缩放实现 **缩圈**（UV 从 1.35 收到 1.0 表示从大到小）。

### 6.5 绘制顺序（buildNoteVertices）

1. **格子背景** — 全矩阵铺 layer 5；活跃 4 列更亮（alpha 0.85 vs 0.35）
2. **Note 本体** — Tap/Hold；跳过已判定（`colHeads`）
3. **Approach 缩圈** — 批量 deferred；基于 AR 计算 `timeDiff`
4. **Hold 进度** — 270° 起顺时针填充
5. **击中特效** — Perfect/Good 的 Tap 扩散环

### 6.6 滚动与 Formation 的视觉

- **滚动**：`Renderer::setScrollState()` → ease-in-out 插值 `scrollOffset`
- **Formation**：`setAnimParams()` 传递旋转/透明度/行列 slide 给 NoteRenderer

### 6.7 纹理加载

- `texture_cache.cpp` — 路径 → OpenGL 纹理 ID 缓存
- `stb_image_impl.cpp` — stb_image 单文件实现

---

## 7. 谱面与转换管线

### 7.1 数据结构

**Note**（`note.h`）：

```cpp
time, col, row, type(T/H), holdEnd, ...
```

**Formation**（`beatmap.h`）：

```cpp
time, rows, cols, transformType, transformDurationMs, blockSize
```

**Beatmap**：notes + formations + difficulty + meta + audioPath

### 7.2 OsuParser 转换流程

```
.osu 文本
  → parseTimingPoints / parseHitObjects
  → makeConvertedNotes()           // 窗口 earliest/latestHit、Hold endTime
  → generateFormationsAndFilter()  // 密度阵型 + 滚动模拟 + 冲突丢弃
  → arrangeRemainingNotes()        // 同列 1/4 拍密度编排 + 稳定列规则
  → BeatmapBuilder
```

**密度 → 阵型**（`targetShapeForDensity`）：±1500ms 内 note 数 → 2×3 … 4×6。

**滚动模拟**（`scroll_simulation.h` + `resolveScrollConflict`）：

- 窗外 note 需滚动才能打到 → 检查 Hold 阻塞 + 动画时长能否赶上 `earliestHit`
- 失败则 **丢弃目标 note**（不是丢 Hold）

**编排**（`arrangeRemainingNotes`）：

- **节奏窗**：按当前 TimingPoint 的 BPM 动态计算 `arrangeRhythmWindowMs` = `msPerBeat / 分母`
  - BPM ≥ 140 → **1/3 拍**（分母 3）
  - 100 ≤ BPM < 140 → **1/4 拍**（分母 4）
  - BPM < 100 → **1/6 拍**（分母 6）
- **密度阈值** `kArrangeColDensityThreshold = 2`：同列节奏窗内已有 1 个 note + 当前 → 触发重排
- **列选择**：默认在整段活跃窗内按负载均分；若存在「边缘列 + 无关滚动在 latestHit 前发生」→ 仅 **稳定列**
- **稳定列**（滚后仍留在窗内）：0–3→1–3，1–4→2–3，2–5→2–4

### 7.3 MMA 格式

文本分段：`[General]`、`[Difficulty]`、`[Formations]`、`[Notes]` 等。详见 `mma_parser.cpp` / `mma_serializer.cpp`。

---

## 8. 判定与游玩循环

### 8.1 PlayingState::update 阶段

1. 同步时钟、lead-in 倒计时
2. 判定（非滚动/非 formation 锁定）
3. 滚动检测与完成
4. 同步 Renderer（scroll、colHeads、hitEffects、fade）
5. FormationController 更新
6. HP 流失
7. 结束检测 → Result

### 8.2 输入路径

```
SDL_KEYDOWN (Kernel::pumpInputEvents)
  → dispatchGameplayKeyEvent
  → PlayingState::handleKeyEvent(timestamp)
  → getKeyMapping()[key] → column
  → JudgeQueue::onKeyPress(judgeTime, col, od)
  → handlePressResult → 音效/特效/分数
```

按键在 **事件到达时** 立即判定（非等 fixed update），时间戳由 `Clock::songTimeAtTickMs()` 映射到歌曲时间。

---

## 9. 音频系统

**文件**：`audio/audio_engine.cpp`

| 能力 | 实现 |
|------|------|
| BGM 流式播放 | miniaudio `ma_decoder` + 循环读 buffer |
| 预览 | SongSelect 用，可循环 preview 段 |
| 音效 | 每类型 3 实例 round-robin，避免叠音截断 |
| HitNormal 发现 | 扫描 `res/` 下 `*_normal.wav` 等 |
| 音量 | 全局 + Preview/BGM/Effect 分类 |

**Clock 同步**：仅在音频 cursor **前进** 时更新 anchor，避免插值落后 ~5ms。

---

## 10. CMake 构建说明

### 10.1 顶层 CMakeLists.txt

| 设置 | 值 |
|------|-----|
| C++ 标准 | 17 |
| 输出目录 | `build-*/bin`、`build-*/lib` |
| 源文件收集 | `GLOB_RECURSE src/*.cpp src/*.h` |
| WIN32 子系统 | `add_executable(... WIN32)` — Windows 无控制台 |

**生成版本头**：`version.h.in` → `build/generated/version.h`

**Post-build**：

- 复制 SDL2.dll、assets/、res/

### 10.2 选项

```cmake
option(MM_BUILD_TESTS "Build unit tests" OFF)
```

开启后引入 Catch2 并 `add_subdirectory(tests)`。

### 10.3 常见构建命令

```powershell
# Debug
cmake --build build-vs --config Debug

# Release
cmake --build build-vs --config Release

# 重新配置
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
```

### 10.4 包含路径

- `src/` — 源码根
- `build/generated/` — version.h
- `third_party/`、`third_party/stb/`
- SDL2 include（兼容 `#include <SDL.h>` 写法）

---

## 11. 第三方库

| 库 | 用途 | 集成方式 |
|----|------|----------|
| **SDL2 2.30.8** | 窗口、GL 上下文、输入、计时 | 预编译 lib + DLL |
| **GLAD** | OpenGL 3.3 Core 函数加载 | `glad_add_library(glad_gl33_core ...)` |
| **GLM** | 矩阵/向量 | header-only / add_subdirectory |
| **Dear ImGui** | UI | 静态库 + SDL2/OpenGL3 backend |
| **miniaudio** | 音频 | header-only，`MINIAUDIO_IMPLEMENTATION` 在 audio_engine.cpp |
| **stb_image** | 读 png/jpg | `stb_image_impl.cpp` |
| **Catch2** | 测试 | 仅 MM_BUILD_TESTS |

依赖定义见 `cmake/Dependencies.cmake`；缺失时部分功能降级（如 GLAD placeholder 仅链接 opengl32）。

---

## 12. 配置与资源路径

**配置文件**：exe 同目录 `config.ini`（`platform/config.cpp`）

常见键：

| 键 | 含义 |
|----|------|
| `gameplay.timing_offset_ms` | 判定时间补偿 |
| `gameplay.key_offset` | 按键映射到活跃列的偏移 |
| `gameplay.debug_hud` | 调试 HUD 开关 |
| `video.*` | 分辨率、全屏、背景 dim |

**资源**：

- `res/` — 通用贴图与音效
- `assets/beatmaps/<曲名>/` — `.mma`、`audio.mp3`、背景图

**日志**：`logs/melody_matrix.log`

---

## 13. 扩展阅读

| 文档 | 内容 |
|------|------|
| `docs/fixed_cell_grid_design.md` | 固定 256px 逻辑格设计 |
| 源码内中文注释 | 各 `.cpp/.h` 文件头与函数说明 |

**建议阅读顺序**（入门）：

1. `main.cpp` → `kernel.cpp` → `state_manager.cpp`
2. `playing_state.cpp`（可先读 `initGameplay`、`update`、`handleKeyEvent`）
3. `judge_queue.cpp` + `judge_strategy.h`
4. `note_renderer.cpp` + `grid_layout.h`
5. `osu_parser.cpp`（`parse`、`generateFormationsAndFilter`、`arrangeRemainingNotes`）
6. `scroll_simulation.h`

---

## 14. 源码中文注释说明

`src/` 下全部业务源码已按统一规范补充中文注释（不含 `third_party/`）。

### 14.1 注释层级

| 层级 | 内容 | 示例位置 |
|------|------|----------|
| **文件头** | 职责、依赖、在项目中的用法 | `kernel.cpp`、`judge_queue.h` |
| **类/结构体** | 用途与协作关系 | `JudgeQueue`、`GridLayout` |
| **函数** | 参数、返回值、调用时机 | `PlayingState::checkAndTriggerScroll` |
| **算法块** | 大函数内 `═══` 分区说明 | `osu_parser.cpp`、`playing_state.cpp` |
| **行内** | 非显然逻辑、公式含义 | 滚动/Hold 阻塞、判定窗口 |

### 14.2 模块索引（按目录）

| 目录 | 主要内容 |
|------|----------|
| `core/` | Kernel 主循环、Clock、StateManager、各 GameState |
| `beatmap/` | MMA/osu 解析、转换、滚动模拟、编排 |
| `gameplay/` | JudgeQueue、分数/连击/HP、FormationController |
| `renderer/` | OpenGL 实例化、GridLayout、NoteRenderer 层 |
| `audio/` | miniaudio 封装、BGM/预览/SFX |
| `platform/` | config、文件系统、zip、对话框 |
| `ui/` | ImGui 主题与管理 |
| `util/` | Logger、Result、EventManager、Hash |

### 14.3 阅读建议

大文件（`playing_state.cpp`、`song_select_state.cpp`、`osu_parser.cpp`）采用 **分区标题 + 函数文档**，未对每一行 `{`/`}` 加注，以免干扰阅读。入门时配合本文档第 5–8 节与文件头「主要算法块」列表跳转即可。

---

*文档版本与项目 0.1.0 同步；如有行为差异以源码为准。*
