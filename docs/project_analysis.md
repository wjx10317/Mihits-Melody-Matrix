# Melody-Matrix 项目分析报告

---

## 目录

1. [外部库导入方法](#一外部库-sdl--imgui--opengl-的手动导入方法)
2. [SDL/ImGui/OpenGL 调用关系](#二sdl--imgui--opengl-三者的使用关系与界面生成流程)
3. [核心算法模块实现](#三内部主要算法模块实现)
4. [song_select 状态额外说明](#四song_select-状态的额外说法)
5. [状态机切换](#五状态机的切换)

---

# 一、外部库 SDL / ImGui / OpenGL 的手动导入方法（及对其他第三方库的通用性）

项目采用 **vendored(源码内嵌)模式**，不使用 vcpkg/conan 包管理器。配置集中在 `cmake/Dependencies.cmake`，由 `CMakeLists.txt:27-28` `include()` 引入。所有库期望存放在 `third_party/` 下。

## 1. SDL2 — 预编译 MSVC 开发包 + find_package

**位置**：`third_party/SDL2-2.30.8/`（含 `include/`、`lib/x64/SDL2.lib`、`SDL2main.lib`、`SDL2.dll`）

**配置**：`cmake/Dependencies.cmake:14-41`

```cmake
set(SDL2_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.30.8")
find_package(SDL2 REQUIRED NO_DEFAULT_PATH PATHS "${SDL2_DIR}" NO_CMAKE_FIND_ROOT_PATH)
# 若 devel 包未提供 SDL2::SDL2 target，则手动创建 IMPORTED STATIC target 指向 lib/x64/SDL2.lib
```

**手动导入要点**：
1. 下载 SDL2 devel MSVC 包(如 `SDL2-devel-2.30.8-VC.zip`)解压到 `third_party/SDL2-2.30.8/`
2. `find_package(SDL2 ... NO_DEFAULT_PATH PATHS ...)` 限定只在 vendored 目录查找
3. 链接 `SDL2::SDL2` + `SDL2::SDL2main`(见 `CMakeLists.txt:51-58`)
4. 通过 `add_custom_command(POST_BUILD)` 将 `SDL2.dll` 拷贝到可执行目录(`CMakeLists.txt:65-73`)

## 2. ImGui — 源码集成(非 find_package)

**位置**：`third_party/imgui/`（docking 分支）

**配置**：`cmake/Dependencies.cmake:108-133`

```cmake
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp      # SDL2 backend
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp   # OpenGL3 backend
)
target_include_directories(imgui PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_link_libraries(imgui PUBLIC SDL2::SDL2)
```

**手动导入要点**：
1. 克隆 imgui(docking 分支)到 `third_party/imgui/`
2. 把 5 个核心 .cpp + `backends/imgui_impl_sdl2.cpp` + `backends/imgui_impl_opengl3.cpp` 编为一个 STATIC 库
3. backend 目录通过 `target_include_directories(imgui PUBLIC ...)` 暴露给主项目，使 `src/ui/ui_manager.cpp` 可直接 `#include "imgui_impl_sdl2.h"` / `"imgui_impl_opengl3.h"`
4. 不需 `find_package`

## 3. OpenGL 加载库 — GLAD(实际为手写 placeholder)

**重要发现**：项目配置了 GLAD，但实际走的是**手写 placeholder** 分支，并非真正的 glad2 生成代码。

**位置**：`third_party/glad/`（实际是 glad2 Python 源码包，**顶层无 CMakeLists.txt**）

**配置**：`cmake/Dependencies.cmake:59-85`

```cmake
if(EXISTS "${GLAD_DIR}/CMakeLists.txt")
    glad_add_library(glad_gl33_core STATIC API gl:core=3.3)   # 真正的 glad2(未走到此分支)
else()
    # Fallback: 用模板生成占位 GL loader
    configure_file("${CMAKE_SOURCE_DIR}/cmake/glad_placeholder.h.in" "${GLAD_GEN_DIR}/glad.h")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/glad_placeholder.c.in" "${GLAD_GEN_DIR}/glad.c")
    add_library(glad_gl33_core STATIC "${GLAD_GEN_DIR}/glad.c")
    target_link_libraries(glad_gl33_core PUBLIC opengl32)  # 链接系统 opengl32
endif()
```

**手动导入要点**(若想用真正的 glad2)：
1. 在 `third_party/glad/` 顶层放 `CMakeLists.txt`(glad2 上游 cmake 目录有)，或执行 `python -m glad --api gl:core=3.3 --out-path build/generated/real_glad c`
2. 若维持现状：修改 `cmake/glad_placeholder.h.in` / `.c.in`(共 ~380 行)新增 GL 函数指针
3. **未使用 GLEW/gl3w**，仅 ImGui 内部用其自带 `imgui_impl_opengl3_loader.h`(gl3w_stripped)，两套 loader 通过 static 符号隔离共存

## 4. 对其他第三方库的方法是否通用？

**不通用，按库形态分四种策略**：

| 库形态 | 项目示例 | CMake 策略 | 关键配置 |
|--------|---------|-----------|---------|
| **预编译二进制** | SDL2 | `find_package(NO_DEFAULT_PATH PATHS ...)` + IMPORTED fallback | `cmake/Dependencies.cmake:14-41` |
| **源码 + backend** | ImGui | 编为 STATIC 库，PUBLIC 链接依赖 | `cmake/Dependencies.cmake:108-133` |
| **单头文件库** | miniaudio, stb_image | IMPORTED INTERFACE target，仅 include_directories | `cmake/Dependencies.cmake:87-106`；`CMakeLists.txt:36-41` |
| **源码 + 自带 CMakeLists** | glm, Catch2 | `add_subdirectory`，注意 `GLM_TEST_ENABLE OFF` | `cmake/Dependencies.cmake:43-57` |
| **特殊** | GLAD | 走 placeholder fallback，手维护 .in 模板 | `cmake/Dependencies.cmake:59-85` |

**最终链接汇总**：`CMakeLists.txt:51-58`
```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL2::SDL2 SDL2::SDL2main glm::glm imgui miniaudio::miniaudio glad_gl33_core
)
```
Windows 下额外链接 `Comdlg32`(原生文件对话框 `GetOpenFileName`)。

---

# 二、SDL / ImGui / OpenGL 三者的使用关系与界面生成流程

## 1. 三层架构

```
SDL2(窗口/输入/GL 上下文)  ─┐
                            ├─→ 共享同一 GL 上下文 → 显示
OpenGL 3.3 Core(游戏世界)  ─┘
                  ↑
ImGui(SDL2+OpenGL3 backend)→ 菜单/HUD 叠加层
```

- **SDL2**：提供窗口、GL 上下文、事件输入
- **OpenGL 3.3 Core**（通过 glad 加载函数指针）：绘制背景图、判定矩阵、note 等游戏内容
- **ImGui**：经 SDL2 + OpenGL3 backend 接入，绘制菜单/HUD/对话框

## 2. 初始化流程(`src/core/kernel.cpp:112-235`)

### SDL 初始化

```cpp
SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");                  // :118
SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);                  // :120
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);                         // :153
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);                        // :154
SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

m_window = SDL_CreateWindow("Melody Matrix",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, createW, createH,
    SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);  // :173

m_glContext = SDL_GL_CreateContext(m_window);                                 // :189
gladLoadGLLoader(static_cast<GLADloadproc>(SDL_GL_GetProcAddress));            // :197
SDL_GL_SetSwapInterval(1);                                                    // :215 启用 VSync
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
glClearColor(0.102f, 0.102f, 0.180f, 1.0f);                                    // #1a1a2e
```

### ImGui 初始化(`src/ui/ui_manager.cpp:14-54`)

```cpp
IMGUI_CHECKVERSION();                                                         // :23
ImGui::CreateContext();                                                       // :24
ImGuiIO& io = ImGui::GetIO();
io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;                         // :27
io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;                           // :28
io.IniFilename = nullptr;                                                     // :29 禁用 .ini 持久化
ImGui_ImplSDL2_InitForOpenGL(window, glContext);                              // :32 绑定 SDL2 后端
ImGui_ImplOpenGL3_Init("#version 330 core");                                  // :36 GLSL 3.3 core
Theme::apply();                                                               // :42 应用霓虹主题
```

**关键**：GLSL 字符串 `"#version 330 core"` 与 SDL_GL_SetAttribute 的 3.3 Core Profile 严格对齐。

## 3. 主循环协作顺序(`src/core/kernel.cpp:237-282`)

```
while (m_running) {
  1. 计算 deltaTime(防 spiral of death，clamp 到 0.25s)
  2. pumpInputEvents()  (kernel.cpp:361)
     └─ while SDL_PollEvent:
        ImGui_ImplSDL2_ProcessEvent(&event)   // 先让 ImGui 消费输入
        switch(event.type): QUIT/KEYDOWN/KEYUP/WINDOWEVENT
  3. 固定步长 update(1/240s)
     └─ m_stateManager.update(FIXED_DT)
  4. 渲染：
     4a. SDL_GL_GetDrawableSize → glViewport → glClear(COLOR|DEPTH)   // :266-268
     4b. m_renderer.renderFrame(interpTime)   // OpenGL 直接绘制游戏内容  // :272
         └─ renderBackground + renderGrid + renderNotes
     4c. m_uiManager.newFrame()                                         // :274
         └─ ImGui_ImplOpenGL3_NewFrame + ImGui_ImplSDL2_NewFrame + ImGui::NewFrame
     4d. m_stateManager.render()    // 各状态用 ImGui 构建 UI           // :275
     4e. m_uiManager.renderFrame()                                       // :276
         └─ ImGui::Render + ImGui_ImplOpenGL3_RenderDrawData
  5. SDL_GL_SwapWindow(m_window)                                        // :278
}
```

**关键设计**：
- **OpenGL 游戏内容先绘制进帧缓冲**，ImGui 在其之上叠加（在 NewFrame 之前游戏已写入 FB）
- **ImGui 直接采样 OpenGL 纹理**：MainMenuState 把 `m_bgTexture.textureId()`(GLuint) 强转为 `ImTextureID` 传给 `AddImage`，证明两者共享同一 GL 上下文无缝互操作

## 4. 界面生成的两种模式

**模式 A：ImGui::Begin/End 标准窗口**

各状态 `render()` 方法在 `m_stateManager.render()` 中被调用，用标准 ImGui 控件构建界面：

| 状态 | 文件 | ImGui 形式 |
|------|------|-----------|
| MainMenu | `main_menu_state.cpp:563` | `ImGui::Begin("##MainMenu")` + 按钮；`ImGui::Begin("##SettingsSidebar")`(:744) |
| Playing | `playing_state.cpp:968` | 多个 `ImGui::Begin("##LeadIn"/"##SkipHint"/"##DebugInfo"/"##ScoreHUD"/"##JudgeHUD")` |
| Paused | paused_state.cpp:69,85 | `ImGui::Begin("##PauseOverlay"/"##PauseDialog")` |
| Result | result_state.cpp:67 | `ImGui::Begin("##ResultScreen")` |

**模式 B：DrawList 直接绘制**（绕过 ImGui 窗口系统，直接调 OpenGL）

```cpp
ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(intptr_t)m_bgTexture.textureId(), ...);  // 主菜单背景
ImGui::GetBackgroundDrawList()->AddLine(...) / AddCircleFilled(...)                              // Boot 加载动画
```

## 5. OpenGL 资源初始化

发生在 `Kernel::init` 内通过 `m_renderer.init()`(`kernel.cpp:221`)触发，在 GL 上下文创建之后、ImGui 初始化之前。

**Renderer::init** (`renderer.cpp:14-156`)：
- 编译 grid shader(纯色)、bg shader(纹理 + uAlpha)
- `glGenVertexArrays/glGenBuffers` → m_gridVao/Vbo(动态 256 顶点)、m_bgVao/Vbo(静态 6 顶点全屏四边形)
- `m_bgTexture.loadFromFile("assets/textures/menu-bg.jpg")`(经 stb_image)
- `m_noteRenderer = make_unique<NoteRenderer>() + init()`

**NoteRenderer::init** (`note_renderer.cpp:12-115`)：
- 编译 note shader(`#version 330 core`，location 0-3 = aPos/aInstance/aColor/aTexLayer)
- 5 个 VBO：m_quadVbo(单位四边形)、m_instanceVbo(x,y,w,h)、m_colorVbo(RGBA)、m_layerVbo(纹理层 ID)
- `glVertexAttribDivisor(1/2/3, 1)` 启用**实例化渲染**，一次 `glDrawArrays` 绘制所有 note

## 6. 两套 GL Loader 共存（值得注意）

- **项目自身代码**：用 `glad.h`（placeholder）
- **ImGui 内部**：因未定义 `IMGUI_IMPL_OPENGL_LOADER_CUSTOM`，使用其内置 `imgui_impl_opengl3_loader.h`（gl3w_stripped）

两者通过 static 符号隔离共存，但增加维护复杂度。

---

# 三、内部主要算法模块实现（按步骤说明）

> 项目目录结构：`src/judge/`、`src/clock/`、`src/transform/` 实际并未独立存在，分别归入 `src/gameplay/`、`src/core/`、`src/renderer/`。

## 模块 1：.mma 谱面解析器

**文件**：`src/beatmap/mma_parser.cpp`

**.mma v2 格式规范**：
- 第 1 行必须 `MMA1` 或 `MMA2`
- 6 段：`[General]`、`[Difficulty]`、`[Meta]`、`[FormationTransformMacros]`、`[Formations]`、`[Notes]`

**解析步骤**(`mma_parser.cpp:75-168`)：

1. **逐行预处理**(118-137 行)：`trim` 去空白，跳过空行/`#` 注释；首行强制校验版本头
2. **段落分发**(140-155 行)：遇 `[xxx]` 调用 `flushSection()` 切换 `currentSection` 枚举
3. **段落累积**(157-160 行)：非段落头行累积到 `sectionLines`，等下段或文件结束时统一 flush
4. **flush 分发**(84-116 行)：按 `currentSection` 调用对应子解析器

**各段算法**：
- `parseGeneral`(172-186)：`key=value` 提取 AudioFilename、PreviewTime
- `parseDifficulty`(188-200)：提取 HP、OD、AR 浮点参数
- `parseFormations`(245-306)：核心段
  - `split(line, ',')` 切分，至少 3 字段(time,rows,cols)
  - **多格式兼容**(261-280)：v2 标准 6 字段、v2 短格式(默认 0/0/1.0)、旧 MMA1 7 字段(第 7 位忽略)
  - **blockSize 钳制**(277)：`std::clamp(bs, 0.5f, 1.5f)`
  - **范围校验**(284-285)：`rows=clamp(1,4)`、`cols=clamp(3,6)`
  - **单调性校验**(292-296)：非递减则强制等于上次值
- `parseNotes`(308-345)：至少 3 字段(time,row,col)，第 4 字段类型(`T`/`H`)，Hold 第 5 字段 endTime(缺失默认 `time+500ms`)

## 模块 2：osu! 谱面解析器(x/y → row/col)

**文件**：`src/beatmap/osu_parser.cpp`

**核心算法 `pixelToGrid`**(`osu_parser.cpp:53-65`)：

```cpp
const float cellW = 512.0f / cols;   // 均匀网格列宽
const float cellH = 384.0f / rows;   // 均匀网格行高
outCol = static_cast<int32_t>(std::floor(x / cellW));
outRow = static_cast<int32_t>(std::floor(y / cellH));
outCol = std::max(0, std::min(outCol, cols - 1));   // Clamp
outRow = std::max(0, std::min(outRow, rows - 1));
```

**步骤**：
1. **均匀网格分割**：用 osu 内部分辨率 512×384 / 当前阵型的 cols/rows → 每格像素宽高
2. **Floor 取整**：`floor(x / cellW)` 得列索引，Y-down 坐标系(row 0 在顶)
3. **Clamp 防越界**：保证 511.9 边缘像素不溢出

**动态呼吸矩阵 `transformTypeFor`**(`osu_parser.cpp:383-422`)：根据 `dRows = curr.rows - prev.rows`、`dCols = curr.cols - prev.cols` 分发到 `SLIDE_ROW_*`/`SLIDE_COL_*`/`ROTATE_*`/`SCALE_ONLY`/`NONE`。

## 模块 3：判定系统

**文件**：`src/gameplay/judge_strategy.h`、`src/gameplay/judge_queue.cpp`

### 3.1 判定窗口公式(`judge_strategy.h:32-48`)

- **Perfect 窗口**：`±(22 - 1.05 * OD)` ms → OD=0 时 ±22ms，OD=10 时 ±11.5ms
- **Good 窗口**：`±(65 - 2.6 * OD)` ms → OD=0 时 ±65ms，OD=10 时 ±39ms
- **Miss 阈值**：`goodWindow(OD) + 50` ms

### 3.2 JudgeQueue 数据结构(`judge_queue.h:36-50`)

- **多列独立队列**：`std::array<ColumnQueue, MAX_COLS=8> m_columns`，每列独立 notes 向量 + head 指针
- **ActiveHoldState**(113-121)：每列记录 holding、pressTimeMs、holdEndTimeMs、row、col

### 3.3 自动 Miss — `update`(`judge_queue.cpp:51-82`)

每帧调用，两类自动 Miss：
1. **Hold 超时兜底**(57-69)：`hold.holding && nowMs > holdEnd + goodWindow` → Miss
2. **过期音符 Miss**(71-81)：`while (!col.finished() && nowMs >= note.time + missThreshold)` → emitMiss

### 3.4 按键判定 — `onKeyPress`(`judge_queue.cpp:84-159`)

1. 列越界或队列已结束 → `Ignored`(不消耗按键)
2. `dt = pressTimeMs - note.time`
3. 过早(`dt < -goodWindow`) → `Ignored`(等 update 自动 Miss)
4. Tap 判定：`|dt| <= pw` Perfect；`|dt| <= gw` Good；否则 Ignored
5. **Hold 头部判定**：按下时只判头部时机，进入 holding 状态，记录 holdEndTimeMs，**不 advance head**，立即 commitHit(头部 combo +1)

### 3.5 Hold 释放判定 — `onKeyRelease`(`judge_queue.cpp:161-196`)

- `dt = releaseTimeMs - holdEnd`(以 holdEnd 为基准)
- `|dt| <= pw` Perfect；`|dt| <= gw` Good；过早/过晚释放 → Miss

### 3.6 Hold 双 combo(`playing_state.cpp:920-951`)

**关键设计**：Hold 音符 combo 增加两次 —— 头部按下时一次，尾部释放时再一次。注释明确："尾部释放额外加一次 combo(头部+尾部各一次)"。

## 模块 4：时钟系统

**文件**：`src/core/clock.h`、`src/core/clock.cpp`

**设计原则**："权威游戏时钟由音频播放位置驱动。绝对不要使用帧时间累积进行判定！"

### 4.1 `syncFromAudio`(`clock.cpp:5-15`)

```cpp
int64_t prev = m_audioTimeMs.exchange(audioFrameTimeMs, std::memory_order_acq_rel);
if (!m_paused && audioFrameTimeMs != prev) {       // 仅前进时更新
    m_anchor = std::chrono::steady_clock::now();
    m_anchorAudioMs = audioFrameTimeMs;
}
```

**核心技巧**：使用 `std::atomic::exchange` 原子更新。**仅在音频位置实际前进时**(`audioFrameTimeMs != prev`)才刷新 anchor。

**原因**：音频 cursor 按设备周期更新(~10ms 一次)，但 syncFromAudio 在 240Hz update 中被调用。若每次用停滞的 cursor 重置 anchor，插值会被反复拽回到停滞值，导致时钟落后实际音频位置约半个周期(~5ms)。仅在前进时更新可消除该滞后。

### 4.2 `interpolatedNowMs` — 微秒精度插值(`clock.cpp:21-29`)

```cpp
auto elapsed = std::chrono::steady_clock::now() - m_anchor;
auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
return m_anchorAudioMs + (elapsedUs + 500) / 1000 + m_userOffsetMs;   // +500 四舍五入
```

**算法步骤**：
1. 用 `steady_clock` 测量自上次 anchor 的物理流逝时间
2. 转**微秒**而非毫秒，避免截断造成 ~1ms 落后
3. **四舍五入**：`(elapsedUs + 500) / 1000` 实现 us→ms
4. 加上 m_anchorAudioMs(音频基准) + m_userOffsetMs(用户校准)

### 4.3 PCM frame 转毫秒(`audio_engine.cpp:376-389`)

```cpp
ma_uint64 cursorFrames = 0;
ma_sound_get_cursor_in_pcm_frames(snd.sound, &cursorFrames);
ma_uint32 sampleRate = ma_engine_get_sample_rate(m_engine);
if (sampleRate == 0) sampleRate = 48000;
return static_cast<int64_t>((cursorFrames * 1000 + sampleRate / 2) / sampleRate);  // 四舍五入
```

44100Hz 下精度约 0.02ms。

## 模块 5：音频引擎(miniaudio)

**文件**：`src/audio/audio_engine.cpp`

### 5.1 miniaudio 封装

```cpp
// audio_engine.cpp:96-114
ma_uint32 flags = streaming ? MA_SOUND_FLAG_STREAM : 0;
ma_sound_init_from_file(m_engine, filePath.c_str(), flags, nullptr, nullptr, sound);
```
BGM/Preview 用 streaming=true，SFX 用 streaming=false（全量加载）。`MAX_ACTIVE_SOUNDS=2`。

### 5.2 sound pool(3 实例/类型)全加载策略

**数据结构**(`audio_engine.h:174-178`)：

```cpp
static constexpr int SFX_POOL_SIZE = 3;
ma_sound* m_sfxSounds[static_cast<int>(SfxType::Count)][SFX_POOL_SIZE] = {};  // 4 类型 × 3 实例
int m_sfxRoundRobin[static_cast<int>(SfxType::Count)] = {};
```

**`loadSfx`**(`audio_engine.cpp:443-501`)：
1. **多路径尝试**：`res/xxx`、`../res/xxx`、`../../res/xxx`(适配不同 CWD)
2. **全量加载**：每 SfxType 加载 3 个实例，`flags=0`(非流式)
3. **容错**：某 slot 失败则 break 剩余，loadedCount==0 仅告警

**`playSfx`**(`audio_engine.cpp:503-528`)：
1. **轮转选择**：`slot = m_sfxRoundRobin[idx]; m_sfxRoundRobin[idx] = (slot+1) % SFX_POOL_SIZE`
2. Round-Robin 避免快速连击时第二次播放截断第一次
3. **重置+播放**：`ma_sound_seek_to_pcm_frame(sound, 0)` 回到开头

## 模块 6：矩阵变换动画

**文件**：`src/renderer/renderer.cpp:346-443`、`src/beatmap/beatmap.h:22-40`

### 6.1 transformType 编码体系

| 类别 | 编码范围 | 含义 |
|------|---------|------|
| NONE(0) | 0 | 无变换 |
| SCALE_ONLY | 100 | 仅缩放 blockSize |
| SLIDE_ROW_* | 201-206 | 行增/减 |
| SLIDE_COL_* | 221-226 | 列增/减 |
| ROTATE_* | 301-304 | 行列增减组合 |
| ROTATE_COMPLEX | 399 | 旋转复合(保留) |

### 6.2 六种变换实现

先计算进度 `p ∈ [0,1]` + ease-in-out quadratic 缓动：

```cpp
float easedP = p < 0.5f ? 2.0f * p * p : 1.0f - (-2.0f * p + 2.0f) * (-2.0f * p + 2.0f) / 2.0f;
```

然后按 `transformType` 分类 `cat`(N/S/L/R) 分发：

**① Scale(SCALE_ONLY)**(`renderer.cpp:359-369`)：
```cpp
float interpBlockSize = prevBlockSize + (nextBlockSize - prevBlockSize) * easedP;
m_noteRenderer->setBlockSize(interpBlockSize);
m_noteRenderer->setAnimParams(0.0f, 1.0f, -1, -1, 1.0f, false, false);
m_noteRenderer->render(..., nextRows, nextCols, ...);  // 行列不变
```

**② Slide(SLIDE_ROW/COL_ADD_*)**(`renderer.cpp:370-382`)：传入 prevRows/prevCols 与 easedP 作滑入进度。在 note_renderer.cpp:269-277 中：
- 新行 `r >= animPrevRows`：`slideOffsetX = (1.0f - animSlideProgress) * (-W)` 从左侧滑入
- 新列 `c >= animPrevCols`：`slideOffsetY = (1.0f - animSlideProgress) * (-H)` 从顶部滑下

**③ SlideOut(SLIDE_ROW/COL_REMOVE_*)**(`renderer.cpp:383-401`)：分两段渲染
- 前半段 `p<0.5`：渲染旧矩阵，alpha=1→0 淡出
- 后半段 `p≥0.5`：渲染新矩阵，alpha=0→1 淡入

**④ Rotate(ROTATE_*)**(`renderer.cpp:402-421`)：
```cpp
float angle = p * 2.0f * PI;   // 0 → 2π 完整旋转一周
if (p < 0.5f) {
    setAnimParams(angle, 1.0 - easedP*2.0, ...);   // 旧矩阵 0→180° 淡出
    render(prevRows, prevCols);
} else {
    setAnimParams(angle, (easedP - 0.5)*2.0, ...);  // 新矩阵 180°→360° 淡入
    render(nextRows, nextCols);
}
```

**⑤ 默认切换式(NONE 等)**(`renderer.cpp:422-433`)：中点硬切换行列与 blockSize。

**注意**：用户提到的 6 种中的 ScaleSlide/ScaleRotate，对应 `SLIDE_SINGLE_AXIS_COMPLEX(299)` 和 `ROTATE_COMPLEX(399)`。代码层面 299 的 cat='L' 但不满足 4 个 Slide 子条件会落入"默认切换式"分支；399 走 Rotate 分支。**实际只有 4 种独立动画实现**。

## 模块 7：滚动系统

**文件**：`src/renderer/renderer.cpp:225-235`、`src/core/states/playing_state.h:109-130`、`src/core/states/playing_state.cpp:720-815`

### 7.1 scrollOffset 计算(`renderer.cpp:225-235`)

```cpp
m_scrollOffset = 0.0f;
if (scrolling && m_gridCols > 0) {
    float p = clamp(scrollProgress, 0, 1);
    float easedP = p < 0.5f ? 2.0f * p * p : 1.0f - (-2.0f * p + 2.0f) * (-2.0f * p + 2.0f) / 2.0f;
    const float W = 1920.0f, margin = 120.0f;
    float gw = (W - 2 * margin) / m_gridCols;
    int32_t colDelta = targetStartCol - activeStartCol;
    m_scrollOffset = -static_cast<float>(colDelta) * gw * easedP;
}
```

**关键公式**：`scrollOffset = -colDelta * gw * easedP`
- `gw`(格子宽) = (1920 - 2×120) / m_gridCols
- **符号**：右滚(colDelta>0)时 scrollOffset 为负，矩阵左移（符合"右滚动需矩阵左移"约定）
- **ease-in-out quadratic**：前半段 `2*p²` 加速，后半段 `1-(2p-2)²/2` 减速

### 7.2 触发逻辑 — `checkAndTriggerScroll`(`playing_state.cpp:720-797`)

1. 总列数 ≤ KEY_COUNT(4) 则不滚动
2. 计算 approachMs(同 note_renderer 公式)
3. 遍历所有列队列，找未过期(0 < timeDiff ≤ approachMs)且最早的 note
4. 若最早 note 不在当前窗口，触发滚动
5. **目标窗口居中**：`targetStart = earliestNoteCol - (KEY_COUNT / 2)`，边界约束
6. **自适应滚动时长**：`scrollDuration = availableTime * 0.8f`，clamp [100, 200]

### 7.3 完成逻辑 — `completeScroll`(`playing_state.cpp:799-814`)

```cpp
m_scrollWindow.startCol = newStart;
m_scrollWindow.endCol = newEnd;
m_scrollWindow.scrolling = false;
```

**关键设计**：**不 move note**，note 留在原始列(0-7)，滚动只改 dfjk 按键映射(`getKeyMapping` 返回 `startCol+0..3`)。离开窗口列的未判定 note 由 `JudgeQueue::update()` 自动 miss。

### 7.4 NEXT_SCROLL 时间计算(`playing_state.cpp:1022-1037`)

遍历所有列，找第一个不在当前活跃窗口内的 note，取其时间作为下一次滚动触发时间（HUD 调试用，-1 表示无需滚动）。

### 7.5 主循环集成(`playing_state.cpp:551-560`)

```cpp
if (inScroll && m_scrollWindow.finished(nowMs)) completeScroll();
if (!inScroll && !inTransition) checkAndTriggerScroll(nowMs);
```
**滚动与变换不能重叠**(`!inTransition` 条件)。

## 模块 8：自动播放模块(autoplay)

**文件**：`src/core/states/playing_state.cpp:515-546`

**触发条件**：`!inTransition && !inScroll`（变换与滚动期间不处理 note）。

**精确命中 Perfect 的核心策略**：以**音符自身的理论时间**作为按键时间，使 `dt = pressTime - note.time = 0`，必落在 Perfect 窗口内。

**算法流程**：

```cpp
for (int32_t c = startCol; c <= endCol; ++c) {
    // 1. 优先处理活跃 Hold 的自动释放
    const auto* activeHold = m_judgeQueue.getActiveHold(c);
    if (activeHold && nowMs >= activeHold->holdEnd) {
        auto holdResult = m_judgeQueue.onKeyRelease(activeHold->holdEnd, c, od);  // dt=0
        handleHoldReleaseResult(holdResult, c);
        continue;
    }
    if (activeHold) continue;

    // 2. 处理 Tap 与 Hold 头部
    const auto& note = colQ.front();
    if (nowMs >= note.time) {
        int64_t noteTime = note.time;
        auto result = m_judgeQueue.onKeyPress(noteTime, c, od);   // dt=0
        if (result != JudgmentResult::Ignored) {
            handlePressResult(result, c, noteTime, noteTime);     // timing=0 完美中心
        }
    }
}
```

**双时间锚点**：
- Tap/Hold 头部：pressTime = note.time，dt = 0
- Hold 尾部释放：releaseTime = holdEnd，dt = 0

## 模块 9：note 渲染器

**文件**：`src/renderer/note_renderer.cpp`

### 9.1 AR / approachMs 公式(`note_renderer.cpp:247-248`)

```cpp
float approachMs = 1800.0f - ar * 120.0f;
if (approachMs < 300.0f) approachMs = 300.0f;   // 下限 300ms
```
AR=0 → 1800ms，AR=10 → 600ms。

### 9.2 noteScale 公式(`note_renderer.cpp:390-405`)

```cpp
float approachProgress = 1.0f - (timeDiff / approachMs);
approachProgress = clamp(approachProgress, 0, 1);
float noteScale = 0.3f + 0.7f * approachProgress;   // 0.3→1.0
float alpha = 1.0f;

if (timeDiff <= 0 && !isHolding) {
    float fadeProgress = -timeDiff / 300.0f;
    alpha = 1.0f - std::min(1.0f, fadeProgress);   // 300ms 内线性淡出
    noteScale = 1.0f;
} else if (isHolding) {
    noteScale = 1.0f;
}
alpha *= m_animAlpha * m_globalAlpha;
```

**三阶段视觉**：
- Phase 1 approach(timeDiff > 0)：noteScale 0.3 → 1.0
- Phase 2 hittable(timeDiff ≈ 0)：noteScale=1.0，发光
- Phase 3 expired(timeDiff < 0)：300ms 内线性淡出

### 9.3 判定环收缩(`note_renderer.cpp:411-436`)

```cpp
float ringHalfW = cellHalfW - (cellHalfW - noteHalfW) * approachProgress;
float ringAlpha = 0.8f * colDim;
if (timeDiff <= 0) ringAlpha = 0.0f;   // 过判定时间后环消失
```
判定环从格子边缘线性收缩到音符边缘。

### 9.4 active 列判定与高亮权重

**block(背景块)高亮**(`note_renderer.cpp:255-310`)：

```cpp
float activeLeftX  = W * 0.5f - blockCenterOffset * gw;
float activeRightX = W * 0.5f + blockCenterOffset * gw;
const float blockActiveAlpha = 0.85f;   // 活跃块透明度
const float blockDimAlpha    = 0.35f;   // 灰暗块透明度
```

**跨越边界的拆分渲染**(292-308 行)：用 `overlapLeft/Right` 计算交集，将一个 block 拆分为高亮部分与灰暗部分，实现"一半亮一半暗"过渡效果。

**note 高亮权重**(`note_renderer.cpp:370-383`)：

```cpp
float weight = 1.0f;
if (noteCenterX < activeLeftX) {
    weight = std::max(0.0f, 1.0f - (activeLeftX - noteCenterX) / gw);
} else if (noteCenterX > activeRightX) {
    weight = std::max(0.0f, 1.0f - (noteCenterX - activeRightX) / gw);
}
float colDim = 0.25f + 0.75f * weight;   // 0.25(暗)→ 1.0(亮)
```
colDim 影响 note alpha、判定环 alpha。

### 9.5 cellX 公式(`note_renderer.cpp:347-351`)

```cpp
float cellX = W * 0.5f + (note.col - activeStartCol - noteCenterOffset) * gw + scrollOffset;
```

**关键设计**：滚动期间不更新 `activeStartCol`，仅靠 `scrollOffset` 平移；滚动完成后一次性更新 `activeStartCol` 并归零 `scrollOffset`，两者数学上连续无跳变。

---

# 四、song_select 状态的额外说法

**文件**：`src/core/states/song_select_state.h` / `.cpp`

## 4.1 谱面列表加载 `scanBeatmaps`(`song_select_state.cpp:230-356`)

- **扫描目录**：`m_beatmapDir = "assets/beatmaps"`(默认，可 `setBeatmapDir()` 修改)
- **递归扫描**：`std::filesystem::recursive_directory_iterator`(244 行)
- **文件过滤**：仅 `.mma` 和 `.osu`(扩展名小写化)
- **解析**：`beatmap::createParserForFile(filePath)` 工厂方法选择解析器
- **失败跳过**：单个失败不影响整体扫描
- **构建 BeatmapEntry**：filePath、title、artist、creator、version、difficulty(星级)、ar、od、hp、noteCount、duration、imagePath、audioFilePath、previewTime
- **音频路径解析**：`meta.audioFile` 相对于谱面文件所在目录解析为绝对路径
- **背景图查找**：在谱面目录查找 `background.jpg/png`、`bg.jpg/png`，找不到用默认 `assets/textures/menu-bg.jpg`
- **demo 兜底**：扫描为空时注入一条 "Neon Dreams" demo 数据
- **排序**：按 title 排序，同 title 按 difficulty 排
- **分组**：按 `title + artist` 聚合到 `BeatmapGroup`，每组含多个难度 set

**异步预加载** `scanAndPreload`(`song_select_state.cpp:1270-1282`)：调用 `TextureCache::instance().preload(paths)` 预加载所有分组背景图。

## 4.2 谱面卡片渲染（三层布局）

**渲染分层**（render()第 147-224 行）：
1. `renderImGuiPanel()` 全屏窗口
2. **顶部实心遮罩**：1/4 屏宽直线 → 1/5 屏宽余弦弧过渡 → 剩余薄条，白色描边 + 蓝色内侧描边
3. `renderLeftPanel()`（第 501-735 行）：顶部铺面详情 + 排行榜区域 + 底部 BACK/RANDOM/MOD 按钮
4. `renderRightPanel()`（第 741-1096 行）：可滚动分组列表 + 右下角头像/START 按钮
5. `renderModPopup()`（第 1102-1246 行）：Mod 选择弹窗

**响应式布局** `computeLayout`（第 25-41 行）：基于 1080p 缩放因子 `m_ly.scale = screenH / 1080`，LEFT_RATIO = 0.38f。

## 4.3 选中交互与音效

| 操作 | 代码位置 | 行为 | 音效 |
|------|---------|------|------|
| 组头点击 | 第 847-854 行 | `m_selectedGroup=g; m_selectedSet=0; m_scrollToSelected=true; tryPlayPreview()` | MenuClick |
| set 单击 | 第 972-976 行 | `m_selectedSet=s; tryPlayPreview()` | MenuClick |
| set 双击 | 第 977-982 行 | `m_selectedBeatmap=set.filePath`（触发进入 Playing） | MenuHit |
| RANDOM 按钮 | 第 683-698 行 | 随机选组，避免与当前相同 | MenuClick |
| START 按钮 | 第 1083-1088 行 | `m_selectedBeatmap=selSet->filePath` | MenuHit |
| BACK 按钮 | 第 664-669 行 | 清空选中，`m_nextState=MainMenu` | MenuHit |
| 进入时随机选中 | onEnter 第 78-83 行 | `m_selectedGroup=rand()%size; m_selectedSet=0; tryPlayPreview()` | — |

**音效约定**：
- `menuclick`：切换谱面、浏览类（组头点击、set 单击、RANDOM）
- `menuhit`：选中、确认类（BACK、MOD 切换、双击 set、START、Mod 项激活/取消、CLOSE）

## 4.4 预览音频 `tryPlayPreview`(`song_select_state.cpp:1251-1264`)

- 使用独立的 `m_audio` AudioEngine（注释明示"独立于 PlayingState 的游戏音频"）
- **同路径不重启**：`if (sel->audioFilePath == m_lastPreviewAudioPath) return`
- 否则播放新音频：`m_audio.playPreview(audioFilePath, previewTime, 0.3f, 60000)`（fade 0.3s，循环 60s）

## 4.5 Mod 选择界面

- `m_modPopupOpen` 标志
- **可用 Mod**（onEnter 第 72-75 行）：
  - `{"NoFail", "nofail", false, true}` — implemented=true
  - `{"Autoplay", "autoplay", false, true}` — implemented=true
- **弹窗布局**（renderModPopup 第 1102-1246 行）：400x500 scale 居中，圆角 12、边框 2、青色边框；半透明遮罩 0.5 alpha 覆盖全屏
- **关闭方式**：CLOSE 按钮；点击弹窗外区域
- **`shouldConsumeEscape()`**（song_select_state.h 第 33 行）：`return m_modPopupOpen`，弹窗打开时 ESC 由本状态消费（关闭弹窗），不触发返回上级

## 4.6 谱面信息展示（renderLeftPanel 第 520-597 行）

- 歌名（青色，字号 1.8*scale）
- Artist / Creator（灰白色）
- Length（分:秒）/ Notes 总数
- AR / OD / HP（分别紫/粉/青色标签 + 白色数值）
- 难度名 [version]（粉色）+ 星级 difficulty（金黄色）
- 未选中时显示 "Select a beatmap"

## 4.7 进入 playing 状态时传递参数（update 第 105-125 行）

```cpp
if (!m_selectedBeatmap.empty()) {
    auto* playing = Kernel::instance().stateManager().getStateAs<PlayingState>(GameState::Playing);
    if (playing) {
        playing->setBeatmapFile(m_selectedBeatmap);     // 谱面文件绝对路径
        playing->markNeedsReinit();                     // 标记下次 onEnter 重新初始化
        if (m_selectedGroup >= 0 && m_selectedGroup < (int)m_groups.size()) {
            playing->setBackgroundImage(m_groups[m_selectedGroup].imagePath);  // 组级背景
        }
        std::vector<std::string> activeMods;
        for (const auto& mod : m_mods) {
            if (mod.active && mod.implemented) activeMods.push_back(mod.id);
        }
        playing->setMods(activeMods);                   // 激活模组 ID 列表
    }
    return GameState::Playing;
}
```

**传递四项数据**：setBeatmapFile()、markNeedsReinit()、setBackgroundImage()、setMods()。

## 4.8 onEnter/onExit 资源管理

- **onEnter**（第 47-84 行）：初始化音频引擎 + loadSfx；首次进入扫描谱面 + 预加载；加载头像（res/icon.png，三路径回退）；初始化 Mod 列表；随机选中并触发预览
- **onExit**（第 86-99 行）：`m_audio.stopWithFade(0.3f)` 淡出 → `m_audio.shutdown()` 关闭；恢复默认背景；保留纹理缓存（注释"不再清空纹理缓存"）

---

# 五、状态机的切换

## 5.1 状态枚举

**文件**：`src/core/game_state.h:8-29`

```cpp
enum class GameState {
    Boot,        // 启动状态，播放"M i hits"加载动画，异步预加载资源
    MainMenu,    // 主菜单
    SongSelect,  // 选歌
    Playing,     // 游玩核心
    Paused,      // 暂停覆盖层
    Result,      // 结算界面
    Count        // 哨兵值：保持当前状态/未进入任何状态
};
```

## 5.2 状态基类设计

**文件**：`src/core/game_state_base.h:9-25`

- `GameStateBase` 抽象基类
- 4 个纯虚接口：`onEnter()`、`onExit()`、`update(float dt)`、`render()`
- `update()` 返回 `GameState`：返回 `Count` 表示保持当前状态，否则请求转换
- 固定步长 240Hz 逻辑更新，可变步长渲染以显示器刷新率调用

## 5.3 StateManager 状态管理器

**文件**：`src/core/state_manager.h` / `.cpp`

- `m_states`：`unordered_map<GameState, unique_ptr<GameStateBase>>` 持有所有状态实例
- `m_currentState`：初始 `Count`（第一次 transitionTo 触发 onEnter）
- `m_pendingState` + `m_transitioning`：过渡期间暂存目标状态
- **`FADE_DURATION = 0.2f`**：200ms 过渡动画时长

**`transitionTo()`**(`state_manager.cpp:11-32`)：
- 同状态直接返回
- 首次切换(`m_currentState == Count`)直接执行，无过渡动画（BootState 特例）
- 否则启动 `m_transition.startFadeOut()`，记录日志

**`executeTransition()`**(`state_manager.cpp:34-64`)：
- 调用当前状态 `onExit()`
- 切换 `m_currentState`
- 调用新状态 `onEnter()`
- **同步渲染标志**：`isGameplay = (newState == Playing || newState == Paused)`，调用 `renderer.setGameplayRendering(isGameplay)`
- 离开游戏状态时清除背景纹理 `renderer.setBackgroundPath("")`

**`update()`**(`state_manager.cpp:66-93`)：
- 先更新过渡动画：淡出完成 → executeTransition → 开始淡入
- 过渡期间仍更新当前状态（保持动画运行）
- 调用当前状态 update(dt)，若返回新状态则调用 transitionTo()

**`getStateAs<T>()`** 模板方法：用于状态间数据传递（SongSelect → Playing 传递参数核心机制）。

## 5.4 主流程状态切换图

```
Boot ──(动画+扫描+纹理完成)──> MainMenu ──(PLAY)──> SongSelect
                                                       │
                                          (START/双击) │ (BACK/ESC)
                                                       ▼
                                              ┌─── Playing ───┐
                                              │       │        │
                                       (ESC)  │       │(曲终/HP=0)│
                                              ▼       │        │
                                            Paused    ▼        ▼
                                              │     Result    (无)
                                  (RESUME)────┘       │
                                              (RETRY) │ (BACK)
                                                      ▼
                                                  Playing/SongSelect
```

## 5.5 各转换的代码位置

| 转换 | 触发 | 代码位置 |
|------|------|---------|
| Boot → MainMenu | 动画完成+扫描完成+纹理完成 | `boot_state.cpp:73` `return GameState::MainMenu` |
| MainMenu → SongSelect | PLAY 按钮 | `main_menu_state.cpp:603` `m_nextState = GameState::SongSelect` |
| SongSelect → Playing | START/双击 set | `song_select_state.cpp:124` `return GameState::Playing` |
| SongSelect → MainMenu | BACK 按钮 | `song_select_state.cpp:668` `m_nextState = GameState::MainMenu` |
| Playing → Paused | ESC 键 | `kernel.cpp:383` `m_stateManager.transitionTo(GameState::Paused)` |
| Paused → Playing | RESUME/RETRY 按钮 | `paused_state.cpp:24/33` `return GameState::Playing` |
| Paused → SongSelect | QUIT 按钮 | `paused_state.cpp:43` `return GameState::SongSelect` |
| Playing → Result | 曲终 / HP=0 | `playing_state.cpp:698` `return GameState::Result` |
| Result → Playing | RETRY 按钮 | `result_state.cpp:32` `return GameState::Playing` |
| Result → SongSelect | BACK 按钮 | `result_state.cpp:41` `return GameState::SongSelect` |

## 5.6 ESC 特殊处理(`kernel.cpp:378-394`)

ESC 不进入状态 update() 处理，而在 Kernel 事件泵中直接处理：
- **MainMenu** → `m_running = false`（退出程序）
- **Playing** → `transitionTo(GameState::Paused)`
- **Paused** → `transitionTo(GameState::Playing)`
- **SongSelect** → 查询 `shouldConsumeEscape()`：若 mod 弹窗打开则消费 ESC（关闭弹窗），否则 `transitionTo(GameState::MainMenu)`

## 5.7 特殊过渡状态

**无独立 Loading/Transition 状态**。过渡效果通过 `SceneTransition` 内嵌在 StateManager 中：
- 200ms 淡出 → executeTransition 实际切换 → 200ms 淡入
- 过渡期间仍调用当前状态 update(dt)（保持动画运行）
- **首次切换特例**：从 Count 进入 Boot 时直接 executeTransition，无过渡动画

## 5.8 资源加载/卸载时机

**加载**：
- BootState 异步线程调用 `songSelect->scanBeatmaps()`（`boot_state.cpp:46`），仅文件 I/O + 解析（无 GL 上下文）
- 主线程完成后 `TextureCache::instance().preload(paths)` 预加载图片（`boot_state.cpp:60`）
- SongSelectState::onEnter 若 `!m_scanDone` 重新扫描 + 预加载

**卸载**：
- executeTransition 中：若 newState 非 Playing/Paused，调用 `renderer.setBackgroundPath("")` 清除背景
- SongSelectState::onExit 关闭音频引擎，但保留纹理缓存
- PlayingState::onExit 仅 `m_audio.pause()` + `clock().pause()` + `SDL_StartTextInput()`，不清除背景和 gameplay 渲染（暂停时仍需显示）
- **关键**：从 Paused/Result 退出到 SongSelect 时，PlayingState::onExit 不会被调用（因为当前状态是 Paused/Result）。所以 `paused_state.cpp:40` 和 result_state.cpp 都显式调用 `playing->cleanupRenderer()` 清除渲染资源

## 5.9 playing 状态的子状态（无显式枚举）

Playing/Paused 在状态机层面是平级状态。PlayingState 内部通过多个 bool 成员模拟子状态（`playing_state.h:75-95`）：

| 成员 | 含义 |
|------|------|
| `m_gameplayInitialized` | 游戏是否已初始化（区分首次进入与 resume） |
| `m_needsReinit` | 是否需要重新初始化（retry 标志） |
| `m_leadInActive` | 前导等待阶段（第一个 note 前 3 秒） |
| `m_matrixVisible` | 矩阵是否可见（接近第一个 note 时显示） |
| `m_songFinished` | 歌曲结束（触发 Result） |
| `m_playerDied` | HP 归零（触发 Result） |
| `m_scrollWindow.scrolling` | 列滚动窗口切换中 |
| `m_autoplay` | Autoplay 模组启用 |

**Pause/Resume 子逻辑**（onEnter 第 23-49 行）：
- 若 `m_needsReinit && m_gameplayInitialized` → 调用 `resetGameplay()` 完整重置
- 若 `m_gameplayInitialized && !m_songFinished` → 仅 `m_audio.resume()` + `clock().resume()` + 重置按键状态（resume 路径）
- 否则调用 `initGameplay()` 全新初始化

## 5.10 编辑模式（不存在）

完整检索 `src` 目录下 `EditState|edit_mode|EditMode|edit_state` 无任何匹配。`GameState` 枚举仅 6 个状态，无 Edit 项。项目中没有任何与编辑模式相关的实现。

---

# 总结

整个 Melody-Matrix 项目：
- **构建**：vendored 模式，无包管理器，4 种 CMake 策略应对不同库形态
- **架构**：SDL2(窗口/输入) + OpenGL 3.3(游戏世界) + ImGui(菜单/HUD) 共享同一 GL 上下文
- **算法**：osu!mania 风格判定(22-1.05×OD / 65-2.6×OD)、音频驱动时钟(微秒插值+仅前进时更新 anchor)、6 种矩阵变换动画(ease-in-out quadratic)、ease-in-out 滚动(不 move note 只改映射)
- **状态机**：State 模式 + 自驱动转换，6 状态 + 内嵌 SceneTransition 过渡，getStateAs<T> 直接传递参数
- **设计紧凑**：算法分工明确，代码位置清晰
