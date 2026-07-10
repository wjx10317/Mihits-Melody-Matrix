## Malody-Martix项目知识点总结

## 音频引擎问题

### 1. 为什么选择 miniaudio？

选择 miniaudio 的原因：开源；集成简单（单头文件）；低延迟且缓冲区/period 可配置（本项目 `ma_engine_init(nullptr)` 用默认配置，但库支持按需求调参）。声卡无法逐采样播放，需在内存环形缓冲区批量供数，回调按块填充空位。period 小 → 延迟低但更易断音；缓冲区大 → 抗卡顿但延迟高，音游倾向小 period。格式支持 mp3/wav 等；ogg 我本地测过不理想，未深究。API 简洁，便于学习和维护。

### 2. 为什么声卡没法逐字节/逐采样播放？

OS 音频栈 + 声卡 DMA 按块搬运内存到 DAC，有最小传输粒度，不能每次只搬 1 字节/1 采样。DAC 输出连续模拟波形，需要稳定采样流。缓冲区以帧为单位组织。miniaudio 内部常用 f32 混音，再量化到设备支持的位宽（如 16/24 bit），动态范围更好、量化失真更小。

### 3. 既然用了 SDL，为什么不用 SDL_Audio？

miniaudio 的 `ma_engine` 更适合 BGM 流式 + seek（选歌预览）、多路 `ma_sound` 并发；SDL_mixer 偏上层封装，seek/多路混音要自己补。SDL 音频 callback 也能混，但要自己管解码、多路相加、播放进度；queue 模式也难精确对齐 不可控。SDL 负责窗口/GL/输入/时间，音频交给 miniaudio，做到了职责分离。

### 4. 混音怎么做？

混音交给 miniaudio `ma_engine` 内部 node graph。`AudioEngine` 只负责挂 `ma_sound`、音量、start/stop/seek；多路在图里 采样相加（含增益），不是业务层手动混成一条 buffer。

### 5. node graph 是什么？

`ma_engine` 内部流水线：每路声音是 node，经 bus 连到输出；音频线程每个 period 从源到终点走一遍（解码、增益、相加）。这是 miniaudio 天然支持 BGM + 多音效并发的原因。

------

## 窗口库选择问题

### 6. 为什么选 SDL？

跨平台；资料多；能创建 OpenGL 上下文；支持窗口/桌面全屏与分辨率切换。对比 Qt：Qt 是 retained-mode 应用框架，不适合每帧重建 gameplay UI；事件循环延迟难控。本项目 SDL + ImGui 每帧即时模式 UI，与 GL 同上下文，主循环轮询次数可控。

### 7. 本项目实际用了 SDL 哪些能力？

集中在 `kernel.cpp` / `ui_manager.cpp` / `clock.cpp`：

① 窗口与 GL：`SDL_CreateWindow(OPENGL|RESIZABLE|HIGHDPI)` → `SDL_GL_CreateContext` → `gladLoadGLLoader(SDL_GL_GetProcAddress)` → `SDL_GL_SetSwapInterval(1)` → `SDL_GL_GetDrawableSize` → `SDL_GL_SwapWindow`

② 显示策略：`SDL_GetDesktopDisplayMode`、`SDL_GetDisplayUsableBounds`、`SDL_WINDOW_FULLSCREEN_DESKTOP`（桌面全屏非独占）、`applyWindowMode()`

③ 输入：`SDL_PollEvent`（KEYDOWN/UP）、`SDL_GetKeyboardState`、`SDL_StopTextInput/StartTextInput`（防 IME 吞键）

④ 时间（辅助）：`SDL_GetTicks64` 供 `Clock` 插值；事件 timestamp 扩 32→64 位对齐按键时刻

⑤ ImGui：`imgui_impl_sdl2` + `imgui_impl_opengl3`

------

## 资源加载问题

### 8. 异步加载资源流程？

发起 `requestLoad`（可在主线程或 Boot 扫描线程） → 路径进 `decodeQueue`，标记 `inFlight`（不是直接进上传队列）→ decode 工作线程 `stbi` 解码 → 像素进 `uploadQueue` → Kernel 主线程每帧 `processPendingUploads(8)` → `glTexImage2D` 进 `TextureCache` → UI `get(path)` 取纹理 ID 绘制。

Boot：`scanBeatmaps` 每组建组时 `requestLoad` 封面；扫描完成后收集路径，轮询 `isLoaded || hasFailed` 再进主菜单。

游玩必需的 note 贴图（tap/slider 等）在 `Renderer::init` `loadSync` 同步解码+上传，不进异步链。

### 9. 为什么上传必须在主线程？

OpenGL 上下文绑定主线程，`glGenTextures` / `glTexImage2D` 必须在有 GL context 的线程执行。

### 10. Boot 动画和加载条怎么实现？

动画：时间轴常量控制 M 折线绘制、i 竖线/点半径、hits 自右滑入（ease-out），用 `ImDrawList` 逐段画线/圆。

进度条：显示 `AssetLoader.progress()`（扫描任务设 0.1→0.8）；纹理就绪由 Boot 另路轮询 `isLoaded/hasFailed`，与进度条数值不是同一条线。进 MainMenu 需：动画最短时长 + 扫描 done + 封面全部 loaded 或 failed。

------

## 渲染库问题

### 11. 为什么选 OpenGL？

瓶颈在 CPU 判定与音画同步，不在 GPU fill rate。Vulkan 描述符/屏障学习成本高；D3D11 绑 Windows；OpenGL 3.3 Core 跨平台、够用。

### 12. 为什么需要 GLAD？

Windows 系统 OpenGL 只导出 1.1；3.3 Core 函数需运行时加载。用 glad 生成 `gl:core=3.3`，`gladLoadGLLoader(SDL_GL_GetProcAddress)`，业务统一 `#include <glad.h>`。

### 13. OpenGL 两种模式？

兼容模式：保留固定管线、立即模式、矩阵栈等。Core 3.3(核心模式)：可编程管线，废弃旧 API，避免混用。

### 14. 立即模式为何废弃？

CPU 逐顶点提交，每帧重复传数据，无 VBO 批量缓存，性能差。现代用 VAO/VBO 一次上传、批量 draw。

### 15. 固定矩阵栈为何废弃？

矩阵在 CPU 用 GLM 算好，经 uniform 传入 shader，灵活且与 3D/2D 统一。

### 16. 窗口/GL 初始化流程？

指定 GL 3.3 Core；双缓冲；color/depth/stencil 位深；创建 context；GLAD 加载；检查版本；`glEnable(BLEND)` + `glBlendFunc`；`SDL_GL_SetSwapInterval(1)`；默认清屏色。

### 17. 双缓冲是什么？

前缓冲显示、后缓冲绘制，一帧完成后 `SwapWindow` 交换，避免单缓冲撕裂。三缓冲：多一块后缓冲，GPU 可不干等 VSync（实现因驱动而异）。每帧：`GetDrawableSize` → `glViewport`（HiDPI 物理像素）→ `glClear` → 绘制 → `SwapWindow`。

### 18. 各模块用了哪些 GL 函数？

对于shader：

| GL 函数                                                | 用途                                      |
| :----------------------------------------------------- | :---------------------------------------- |
| `glCreateShader`                                       | 创建 VS/FS 对象                           |
| `glShaderSource` / `glCompileShader`                   | 上传 GLSL 330 源码并编译                  |
| `glGetShaderiv` / `glGetShaderInfoLog`                 | 编译失败读日志                            |
| `glCreateProgram` / `glAttachShader` / `glLinkProgram` | 链接程序                                  |
| `glGetProgramiv` / `glGetProgramInfoLog`               | 链接失败读日志                            |
| `glDeleteShader`                                       | 链接后删独立 shader 对象                  |
| `glDeleteProgram`                                      | `Shader` 析构 RAII                        |
| `glUseProgram`                                         | `Shader::use()` 激活当前程序              |
| `glGetUniformLocation`                                 | 查 uniform 位置                           |
| `glUniform1i/1f/2f/3f/4f`                              | 传 sampler 单元、alpha、颜色              |
| `glUniformMatrix4fv`                                   | 传 `uProjection` / `uModel`（GLM 列主序） |

项目里 3 套 shader 程序：

NoteRenderer，实例化音符

Renderer背景，全屏贴图

Renderer grid 纯色dim遮罩

对于texture:

| `glPixelStorei(GL_UNPACK_ALIGNMENT, 1)` | 关键：RGB 非 4 对齐宽度时不越界（常见崩溃点）       |
| --------------------------------------- | --------------------------------------------------- |
| `glGenTextures` / `glDeleteTextures`    | 创建/销毁纹理 ID                                    |
| `glBindTexture(GL_TEXTURE_2D, …)`       | 绑定当前 2D 纹理                                    |
| `glTexImage2D`                          | 上传 RGB/RGBA8 像素                                 |
| `glTexParameteri`                       | `CLAMP_TO_EDGE` + `LINEAR` 过滤                     |
| `glGenerateMipmap`                      | note 贴图等 init 时 `loadSync(..., genMipmap=true)` |
| `glActiveTexture` + `glBindTexture`     | `bind(unit)` 绑定到纹理单元 0～15                   |

对于renderer:背景+调度

| GL 函数                                               | 用途                                  |
| :---------------------------------------------------- | :------------------------------------ |
| `glGenVertexArrays` / `glGenBuffers`                  | 创建 bg/grid 的 VAO、VBO              |
| `glBindVertexArray` / `glBindBuffer`                  | 绑定并配置顶点布局                    |
| `glBufferData(..., GL_STATIC_DRAW)`                   | 背景四边形（pos+UV，6 顶点）          |
| `glBufferData(..., GL_DYNAMIC_DRAW)`                  | grid VBO 预分配（dim 遮罩用）         |
| `glEnableVertexAttribArray` / `glVertexAttribPointer` | location 0=pos，bg 还有 location 1=UV |

每帧操作

| `glBindVertexArray(m_bgVao)`       | 绑定背景 VAO                |
| ---------------------------------- | --------------------------- |
| `glDrawArrays(GL_TRIANGLES, 0, 6)` | 1 draw call 画全屏背景      |
| `glBufferData` + `glDrawArrays`    | 可选 dim 遮罩再 1 draw call |

NoteRenderer（`note_renderer.cpp`）

| `glVertexAttribDivisor(loc, 1)`                            | 开启 实例化（Core 3.3 特性）               |
| ---------------------------------------------------------- | ------------------------------------------ |
| `glBufferData(..., GL_DYNAMIC_DRAW)`                       | 每帧上传实例数据                           |
| `glActiveTexture` + 16 路 `bind(0..15)`                    | tap/slider/overlay/holdpush/block          |
| `glUniform1i("uTexTap", 0)` 等                             | 告诉 fragment shader 各 sampler 对应哪单元 |
| `glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instanceCount)` | 1 draw call 画 N 个四边形                  |

对于IMGUi有自己的gl后端

总结一下就是OpenGL 3.3 Core 负责 2D 谱面渲染；GLAD 解决 Windows 函数加载；项目 GL 用法集中在 VAO/VBO、3 套 shader、16 路纹理采样、一次实例化 draw call 画全谱面，背景/Dim 各一次普通 draw；ImGui 在同一帧缓冲上叠加 UI。

------

## 渲染杂项

### 19. VAO 结构

VAO 记录 如何从 VBO 读到 attribute，关键 `glVertexAttribDivisor`：0 = 每顶点变，1 = 每实例变。Note 的 `m_vao` 接 5 路 VBO。

### 20. VBO 结构

本质是 float 数组。Note：1 静态 quad（6 顶点 aPos 0～1）+ 4 动态实例 VBO（xywh、RGBA、层 ID、arcSweep）。背景：interleaved pos+UV，STATIC。dim：仅 pos，DYNAMIC（坐标可 init 写死一次）。

### 21. VS / FS 配合

init 编译 VS+FS → link 成 Program → 删独立 shader 对象。VS 算 `gl_Position` 并传 varying；光栅化；FS 每像素采样/着色；alpha 混合。draw 时 GPU 执行已链好的 Program。

### 22. uProjection 为什么要定义？

把 1920×1080 像素逻辑坐标（Y 向下） 映射到 NDC（约 -1～1）。不投影则像素坐标当 NDC 会被大量裁剪。

### 23. 为什么 vec4 要加 (0, 1)？

齐次坐标：与 4×4 矩阵相乘；z=0（2D 同一平面），w=1（表示点，支持平移/正交除法）。

### 24. 为什么 4×4 不是 2×2/3×3？

2×2 无法表示平移；3×3 可表示 2D 仿射但 OpenGL/GLSL 统一 mat4；兼容 z、透视、depth。

### 25. 矩阵值从哪来？

C++ 侧 GLM `glm::ortho(0, 1920, 1080, 0, -1, 1)` 算出 16 个数，`setMat4` 上传 GPU（不是 GPU 自动算）。`uModel` 为阵型旋转，无旋转时单位阵。

### 26. 为什么两个三角形？

Core Profile 只有点/线/三角形；四边形 = 2 三角、6 顶点。

### 27. CPU → Note 上屏全流程

init：编译 Program + 配 VAO（5 VBO 接线）+ note 纹理 loadSync 上传 + `setTextures` 存指针。

每帧：`buildNoteVertices`（画家算法顺序）→ upload 4 个动态 VBO → bind 纹理 0～15 + `setMat4`/`setInt` → `glDrawArraysInstanced(TRIANGLES, 0, 6, N)` → VS（aPos×实例→像素→NDC）→ 光栅化 → FS（层 ID 选纹理，UV 采样×tint）→ 与 已有帧缓冲（背景/dim/更早实例）alpha 混合。

### 28. 纹理上传与绑定

上传（加载期）：stbi 解码 → `glGenTextures` + `glTexImage2D` → `Texture2D`/`TextureCache` 持 `m_textureId`。note 图 sync；封面 async decode + 主线程 processPendingUploads。

绑定（每帧 draw 前）：`bind(0..15)` + `setInt` 关联 sampler；FS 用 `aTexLayer` 分支采样。一张纹理对应多次 UV 采样（每像素一次）。

## Kernel游戏核心逻辑

### 29.核心循环你是怎么写的？

run()执行之前，初始化窗口，上下文，着色器，ui，注册状态，然后进入run()循环，update逻辑步时固定240hz，通过chrono的steady_clock获取硬件时间，根据实际步长拆分为1/240一步更新逻辑。

逻辑上，处理输入和根据状态机当前状态调用update更新画面，清空后缓冲/有需要上传的纹理/ui渲染/opengl渲染/交换缓冲区

这里我加了一个校验，最长实际帧时间不能超过0.25s，防止卡死死亡螺旋

每帧执行顺序:同步时钟，根据步长拆分1/240更新逻辑，每次update之前轮询一次键盘事件，有事件立刻pause

### 30.音画不同步你是怎么处理的？

耳朵听到的音乐的时间是权威的，所以以音频时钟基准同步，但是通过sdl64ticks平滑插值得到1ms左右粒度。

### 31.为什么用sdl64ticks做时间插值？

因为按键事件带的时间戳是sdl的时间，steady_clock虽然精度更好，但是两套单调时钟起点不同，有常数级别偏移，按键时刻与当前时间点也不再可比，有抖动

### 32.1ms粒度怎么确定的？

SDLTICK64返回的就是ms整数

### 33.什么事死亡螺旋？

设计：逻辑每 1/240 秒 tick 一次（accumulator 攒真实帧时间）

现实：某一帧卡了 2 秒才跑完

若不限制：

frameTime = 2.0

accumulator += 2.0

→ 要在一帧里 while 跑 2×240 = 480 次 update

→ CPU 更忙 → 下一帧更卡 → accumulator 更大 → 永远追不完

这叫 spiral of death（死亡螺旋）：越卡越要补算，越补算越卡。

限制一帧大小，就是限制逻辑步的执行次数，步内syncaudio时直接就获取到当前音频游标了，直接更新至最新逻辑，不会重跑旧逻辑导致永久落后

### 34.已经外推了理论上误差~1ms，那为什么还是判定不准确？

假设音频cursor上报1000ms，但是实际位置可能为1000-1009.9，但是插值只能根据上报值1000计算，天然会有~10ms误差，后面会把同步的单位改为帧，能降到2-3ms，才算够用

### 35.印象深刻的项目难点？

印象最深刻的就是有关判定的精准度问题，多次调整，一开始是每帧处理一次事件，再每逻辑步处理一次时间到事件带上sdl时间戳计算按键对应音频时间计算判定还是有明显抖动误差，为此我加上了很多的debug信息供我调试，(也相当于拓展简单实用的小功能了)其中一个是偏移条，用来直观显示判定点在note判定窗口的具体哪个位置，然后我发现只有固定几个位置可以判定，然后我感觉还是粒度的问题，后面又牵扯到音画不同步，音频时钟更新时机，review代码和执行逻辑加上多轮对话，才找到问题在哪，音频游标的更新频率在~10ms，举一个极端情况下的例子，PCMFRAMES上报当前位置为1000ms，但是由于上报的cursor可能晚于真实解码进度，可能实际位置为1009.9ms，但是sdl插值是基于1000ms计算的，这样判定就会携带误差。

后面迭代时我会改成当前PCM帧数每次sync同步当前帧数计算ms，粒度可以降到2-3ms，对于20-60ms窗口可以接受

### 36.PCM帧不是小数ms级别的吗？为什么误差在10ms？

cursor只在音频线程消费PCM时前进，不是每次访问都更新。具体消费过程是:声卡内部缓冲队列有空时触发miniaudio回调，以块为单位填充PCM帧，所以读cursor有~10ms台阶，再由DMA按块搬运至驱动硬件共用的环形缓冲区，硬件再根据采样率连续读帧模拟电压产生声波发声(单帧只是一个点，连续读帧才能得到声波)

### 37.你觉得你的项目可拓展的东西是什么？

我认为项目最有价值的扩展方向是谱面编辑器，其次是轻量联机；大厅集成我会明确不做。

第一，谱面编辑器（优先级最高）。
当前工程已经具备 `.mma` 原生格式、osu 导入转换和谱面校验，测试也主要覆盖「外部谱面 → 内部格式」这条链路。作为独立节奏游戏，长期不能只依赖其他游戏的谱面适配，需要让玩家能创作、分享、游玩自己的谱面。
技术上可以复用现有 beatmap 数据模型、Formation 系统和验证规则，编辑器侧重点做时间轴编辑、矩阵布局预览和导出校验，这样扩展成本可控，也能形成产品的内容闭环。

第二，服务端联机（中长期）。
如果要走向「完整可联网游戏」，需要补房间、同步、反作弊和谱面分发。联机玩法重要性不高，所以我会把它放在编辑器之后，先保证单机体验和内容生产能力。

第三，关于嵌入游戏大厅：我会主动放弃这条路径。
Qt 侧可以用 `QProcess` 做子进程启动，技术上并不困难。
但如果把音游嵌进游戏大厅，产品形态会变成「大厅 + 子游戏」：用户即使只想玩节奏游戏，也需要额外下载和安装大厅，启动路径多一层，包体和更新链路也更复杂。
这对独立音游来说，会提高用户门槛、拉低首次转化和留存。
所以从产品定位上，我更倾向音游作为独立交付；大厅集成只适合明确的多游戏平台战略，而不适合当前这个项目的核心用户每个游戏阶段场景。

小tips：

逻辑帧固定1/240更新，渲染帧走外侧实际帧。因此暂停时虽然状态换了，但是3D缓冲还在，可以看到暂停之前的游戏画面。先逻辑帧再渲染帧

### 38.状态之间怎么切换的？

用到了状态模式，把每个游戏阶段的行为拆成独立类，换阶段相当于换对象，执行同名不同行为的函数，这样可以不写庞大的switchcase。

有一个状态管理类持有所有注册的状态，采用哈希表存储状态。里面是枚举值对应类指针(unique_ptr)。跨状态访问对应类函数时利用getStateAs模版方法获取对应类指针访问，解耦，举个例子就是pause状态下，可选继续，退出，重新开始，重新开始的话就要访问playing状态调用markneedreinit函数，重新初始化歌谱铺面状态。

### 拓展:模版方法模式：

父类持有骨架(非虚函数里面固定几个虚函数执行流程，子类重写特定虚函数已达到固定执行流程的不同行为)

我这个项目是由context（statemanager，状态模式同一管理状态的类）持有骨架，子类实现对应虚函数，称之为控制反转。

用到的其他设计模式？

Kernel单例模式，uimanager，renderer，Kernel，Audioengine同时也是外观模式，把内部复杂的子系统，比如sdl窗口，gl上下文，imgui双端init/newframe/render,渲染复杂系统，音频流，音频池等简化为简单的api，复杂逻辑不直接暴露出来

策略模式:

在判定中，流程是稳定的，但是判定窗口时可变的，因此让判定类持有判定窗口方法指针，在初始化时注入对象，调用方法时，委托给指针对象调用，这样通过切换指针对象的方式就可以切换判定方法。具体方法时有一个抽象类定义三种窗口的接口，定义子类(方法)重写接口，在initgameplay中创建该对象指针然后注入判定类里面。

是针对同一个东西可能有不同种行为能够灵活切换而存在的，具体做法是有一个抽象类接口，方法重写接口，创建对象注入给管理类(上下文)，上下文调用对应方法时委托给对象执行

为什么使用？

如果不使用，直接改变方法进行替换的话，会有多处修改，且不能游玩时随用户切换，使用的话，切换就只需要在注入时改变注入对象以及定义新方法类，还能随用户切换
