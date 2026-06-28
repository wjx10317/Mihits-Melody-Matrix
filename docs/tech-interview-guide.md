# Melody Matrix（音律列阵）技术深度剖析

> 面试级完整技术文档 | C++17 原生节奏游戏 | 2026年6月

---

## 目录

1. [项目概览](#一项目概览)
2. [技术栈深度剖析](#二技术栈深度剖析)
3. [架构设计](#三架构设计)
4. [功能模块详解](#四功能模块详解)
5. [技术决策对比分析](#五技术决策对比分析)
6. [性能优化策略](#六性能优化策略)
7. [设计模式全景图](#七设计模式全景图)
8. [面试常见问题与回答](#八面试常见问题与回答)

---

## 一、项目概览

### 1.1 项目定位

**Melody Matrix（音律列阵）** 是一款使用 **C++17** 从零构建的 **PC 原生节奏游戏**，核心特色为 **动态呼吸矩阵（Formation）系统** —— 网格布局随音乐疏密实时变化，实现了区别于传统固定轨道的视觉节奏体验。

### 1.2 核心指标

| 指标 | 数值 |
|------|------|
| 编程语言 | C++17（无扩展模式） |
| 源文件数量 | 78 个（51 .h + 42 .cpp，含测试） |
| 代码行数 | ~6200 行 |
| 构建系统 | CMake 3.20+ / Ninja / MSVC 2022 |
| 依赖库数量 | 6 个第三方库（全部 vendored） |
| 游戏状态 | 6 个（Boot / MainMenu / SongSelect / Playing / Paused / Result） |
| 模块化命名空间 | 9 个命名空间 |
| 测试框架 | Catch2（骨架已建立） |

### 1.3 功能全景

```
┌─────────────────────────────────────────────────────────┐
│                    Melody Matrix                         │
├─────────────────────────────────────────────────────────┤
│  启动动画   │ Boot State (M-i-hits 动画 + 异步预加载)    │
│  主菜单     │ MainMenu (开始/设置/导入/退出)              │
│  选歌界面   │ SongSelect (左右分割/排行榜/铺面列表/Mod)   │
│  游戏游玩   │ Playing (音频同步判定/HP管理/连击/阵型)     │
│  暂停面板   │ Paused (继续/重试/退出)                     │
│  结果展示   │ Result (分数/准确度/排行榜)                 │
│  谱面导入   │ .osz ZIP 解压 → OsuParser → MmaSerializer  │
│  动态阵型   │ Formation 系统 (Fade/Insert/Rotate 过渡)   │
│  判定系统   │ Standard/Easy 策略 + Hold 释放判定          │
│  HP/Combo   │ 对数倍率连击 + NoFail 模组                   │
│  设置面板   │ 分辨率/全屏/音量/特效等级/偏移校准           │
│  场景过渡   │ 黑屏淡入淡出 (200ms cubic ease)             │
│  资源管理   │ 纹理缓存/对象池/异步加载                     │
└─────────────────────────────────────────────────────────┘
```

---

## 二、技术栈深度剖析

### 2.1 SDL2 — 窗口管理与输入

#### 2.1.1 为什么选择 SDL2 而非 GLFW / Win32 原语？

| 维度 | SDL2 2.30.8 | GLFW 3.x | Win32 API |
|------|------------|----------|-----------|
| **跨平台** | ✅ Windows/macOS/Linux | ✅ | ❌ 仅 Windows |
| **窗口管理** | 简洁 API，2 行创建窗口 | 简洁 API | 200+ 行样板代码 |
| **DPI 感知** | `SDL_HINT_WINDOWS_DPI_AWARENESS` | `glfwWindowHint(GLFW_SCALE_TO_MONITOR)` | 需手动处理 WM_DPICHANGED |
| **音频支持** | SDL_audio（但本项目未使用） | ❌ 无 | ❌ 无 |
| **游戏手柄** | 内置 SDL_Joystick | 内置 | 需 RawInput/XInput |
| **上下文创建** | SDL_GL_CreateContext | glfwMakeContextCurrent | wglCreateContext |
| **生态/文档** | 游戏行业标准，大量参考 | 简洁但生态较小 | MSDN 文档 |

**选择 SDL2 的核心原因**：它是唯一同时提供窗口管理、OpenGL 上下文创建、输入处理、游戏手柄支持的库，且与 miniaudio 配合时职责边界清晰。

#### 2.1.2 DPI 感知方案

```cpp
// 在 SDL_Init() 之前设置 — 关键时序
SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

/*
 * Per-Monitor V2 模式下：
 * - SDL 所有坐标 API 直接返回物理像素
 * - 不再需要 m_dpiScale / m_logicalWidth 等中间变量
 * - glViewport 使用 SDL_GL_GetDrawableSize（物理像素）
 * - 避免了逻辑像素→物理像素的反复转换
 *
 * 对比传统方案：
 * 方案A（逻辑像素）：需要维护 scale 因子，所有坐标计算需乘除
 * 方案B（本项目）：SDL + OS 直接提供物理像素，零转换开销
 */
```

#### 2.1.3 关键 SDL 调用链

```
启动:
  SDL_SetHint(DPI) → SDL_Init(VIDEO|AUDIO|TIMER) → SDL_GetDesktopDisplayMode
  → SDL_CreateWindow(1920x1080, OPENGL|ALLOW_HIGHDPI|RESIZABLE|HIDDEN)
  → SDL_GL_CreateContext(OpenGL 3.3 Core)
  → SDL_GL_SetSwapInterval(1)  // VSync

主循环每帧:
  SDL_PollEvent(&event)          // 收集输入
  → 分发到 InputManager / ImGui
  → 固定步长逻辑更新 (240Hz)
  → OpenGL 渲染
  → SDL_GL_SwapWindow(window)    // 交换缓冲区

退出:
  SDL_GL_DeleteContext → SDL_DestroyWindow → SDL_Quit
```

#### 2.1.4 窗口尺寸限制与全屏策略

```
clampToUsable():
  SDL_GetDisplayUsableBounds()   // 获取可用区域（扣除任务栏）
  SDL_GetWindowBordersSize()     // 获取标题栏+边框尺寸
  客户区上限 = 可用区域 - 窗口装饰

全屏策略:
  SDL_WINDOW_FULLSCREEN_DESKTOP  // 无边框桌面全屏（不切换显示模式）
  → 不改变显示器分辨率，保留 Alt+Tab 流畅切换
  → 自动切至原生分辨率 NATIVE
```

---

### 2.2 OpenGL 3.3 Core — 渲染管线

#### 2.2.1 为什么选择 OpenGL 3.3 Core 而非 Vulkan / DX11？

| 维度 | OpenGL 3.3 Core | Vulkan | DirectX 11 |
|------|----------------|--------|------------|
| **学习曲线** | 中等 | 极高（1000+ 行初始化代码） | 中等 |
| **项目适配** | ✅ 2D 游戏，draw call < 10/帧 | 过度设计（低负载场景无收益） | ❌ 仅 Windows |
| **驱动支持** | 任何 GPU | 需要较新驱动 | Windows 7+ |
| **调试工具** | RenderDoc, glGetError | 丰富的验证层 | PIX, VS Graphics Debugger |
| **Shader 语言** | GLSL（类 C 语法，快速开发） | SPIR-V（需编译工具链） | HLSL |
| **状态管理** | 全局状态机（需谨慎管理） | 显式状态（无隐式） | 类似 OGL |

**选择理由**：对于每帧 < 10 个 draw call 的 2D 节奏游戏，OpenGL 3.3 的驱动开销完全不是瓶颈。Vulkan 的复杂性（描述符集、管线屏障、内存分配器）带来的认知负担远超收益。

#### 2.2.2 渲染管线架构

```
┌──────────────────── 顶点处理阶段 ────────────────────┐
│  顶点着色器: aPos × uProjection = gl_Position        │
│  实例化: gl_InstanceID → 实例数据数组查找              │
│  点精灵: gl_PointSize 每粒子可调                      │
└───────────────────────────────────────────────────────┘
                          │
                          ▼
┌──────────────────── 图元装配 ─────────────────────────┐
│  三角形(GL_TRIANGLES/FAN) / 线段(GL_LINES) / 点(GL_POINTS)│
└───────────────────────────────────────────────────────┘
                          │
                          ▼
┌──────────────────── 片段处理阶段 ─────────────────────┐
│  纹理采样: texture() 访问 6 个纹理单元                 │
│  层选择: if(vTexLayer==N) → 对应纹理                    │
│  圆形裁剪: discard if dist > 0.5 (粒子)               │
│  Alpha 混合: GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA   │
└───────────────────────────────────────────────────────┘
```

#### 2.2.3 Shader 系统设计

**RAII 包装器**：
```cpp
class Shader {
public:
    // 禁止拷贝 — 唯一所有权
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // 允许移动 — 转移 GL 句柄所有权
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    // 静态工厂 — 处理编译/链接错误
    static Result<Shader> compile(vertexSrc, fragSrc);

    ~Shader() { if (m_id) glDeleteProgram(m_id); }

    // Uniform 设置 — 按名称查找（简洁优先于缓存性能）
    void setFloat(name, v)  { glUniform1f(glGetUniformLocation(m_id, name), v); }
    void setVec4(name, v)   { glUniform4f(glGetUniformLocation(m_id, name), v[0]...); }
    void setMat4(name, ptr) { glUniformMatrix4fv(glGetUniformLocation(m_id, name), 1, GL_FALSE, ptr); }

private:
    uint32_t m_id = 0;
};
```

**编译管线错误处理**：
```
compile(vertSrc, fragSrc)
  ├─ compileShader(GL_VERTEX_SHADER, vertSrc)
  │   ├─ glCreateShader → glShaderSource → glCompileShader
  │   ├─ glGetShaderiv(COMPILE_STATUS) 检查
  │   └─ 失败 → glGetShaderInfoLog → glDeleteShader → return Error
  └─ compileShader(GL_FRAGMENT_SHADER, fragSrc)
      └─ 同上

linkProgram(vs, fs)
  ├─ glCreateProgram → glAttachShader → glLinkProgram
  ├─ glGetProgramiv(LINK_STATUS) 检查
  └─ 失败 → glGetProgramInfoLog → glDeleteProgram → return Error

// 关键：先清理中间 shader 对象再传播 error
// 如果 vs 成功但 fs 失败：先 glDeleteShader(vs) 再 return error
// 这样永远不会泄漏 GL 资源
```

**FallbackShader 单例**：
```cpp
// 当任何自定义 shader 编译失败时，回退到这个纯色 shader
// 保证游戏不会因 shader 问题崩溃
class FallbackShader {
    static Shader s_fallback = createFallback();
    // GLSL: fragColor = uColor  (最简单的着色器)
};
```

**为什么不用 `std::optional<Shader>` 或异常？**

| 方案 | 优点 | 缺点 |
|------|------|------|
| `Result<T>` (本项目) | 类型安全的错误传播，强制检查 | 需模板支持 |
| `std::optional<Shader>` | 标准库 | 丢失错误信息，无法区分失败原因 |
| 异常 | 自动传播 | 着色器失败是常见可恢复错误，不应抛异常 |

#### 2.2.4 实例化渲染（NoteRenderer 核心技术）

**为什么用实例化渲染？**

传统 osu!mania 渲染：每个 note 一个 draw call → N 个 note = N 个 draw call，CPU 瓶颈。

本项目的实例化方案：
```
VAO 布局:
  Location 0: Quad VBO (共享几何体，每顶点)
      [0,0, 1,0, 1,1, 0,0, 1,1, 0,1]  // 单位四边形
      顶点除数 = 0

  Location 1: Instance VBO (每实例，GL_DYNAMIC_DRAW)
      [x, y, w, h] × N  // 每个 note 的位置+大小
      顶点除数 = 1

  Location 2: Color VBO (每实例)
      [r, g, b, a] × N  // 每个 note 的颜色
      顶点除数 = 1

  Location 3: Layer VBO (每实例)
      [texLayer] × N    // 选择纹理 (0=tap, 1=slider, 2=overlay...)
      顶点除数 = 1

一次 draw call:
  glDrawArraysInstanced(GL_TRIANGLES, 0, 6, N)
  // 6 个顶点 × N 个实例 = 一次性绘制所有元素
```

**多纹理层选择机制**（片段着色器中）：
```glsl
uniform sampler2D uTexTap;      // unit 0
uniform sampler2D uTexSlider;   // unit 1
uniform sampler2D uTexOverlay;  // unit 2
uniform sampler2D uTexSPRing;   // unit 3
uniform sampler2D uTexSPFull;   // unit 4
uniform sampler2D uTexBlock;    // unit 5

// 根据 vTexLayer 选择纹理
int layer = int(vTexLayer + 0.5);
if (layer == 5)      color = texture(uTexBlock, vUV);
else if (layer == 0) color = texture(uTexTap, vUV);
else if (layer == 1) color = texture(uTexSlider, vUV);
...
FragColor = color * vColor;  // Tint 调制
```

**为什么不用纹理数组（GL_TEXTURE_2D_ARRAY）？**
- 纹理数组要求所有层同尺寸 — 我们的 6 种纹理尺寸不同
- 多采样器方案更灵活，允许各纹理独立更新

**性能对比**：

| 方案 | Draw Calls | 状态切换 | 适合 N=256 个音符时 |
|------|-----------|---------|-------------------|
| 传统逐 note 渲染 | 256 | 256 次 | ❌ CPU 瓶颈 |
| 实例化渲染（本项目） | 1 | 0 次 | ✅ 极致高效 |

#### 2.2.5 粒子特效（EffectRenderer）

**GPU 圆形粒子**（面试亮点）：
```glsl
// 片段着色器 — 在 GPU 上直接计算圆形
void main() {
    vec2 coord = gl_PointCoord - vec2(0.5); // 相对于中心的坐标
    float dist = length(coord);
    if (dist > 0.5) discard;                // 裁剪正方形外的像素
    float alpha = vColor.a * (1.0 - dist * 2.0); // 径向渐变
    FragColor = vec4(vColor.rgb, alpha);
}
```

**为什么不用纹理四边形做粒子？**
- 点精灵 + GPU 圆形裁剪：无需额外纹理，无纹理采样开销
- 每粒子 `gl_PointSize` 可独立调节大小
- 比传统的带纹理 billboard quad 更简洁

**粒子生命周期**：
```
update(dt):
  位置 += 速度 * dt
  速度.y += 重力(200 px/s²) * dt
  life -= dt / maxLife
  alpha = life（线性衰减）
  size *= (1.0 - dt * 0.5)（指数缩小）

死亡回收:
  remove_if(life <= 0)  // erase-remove 惯用法
  若超过 MAX_PARTICLES(512)：移除最旧粒子（环形缓冲区行为）
```

#### 2.2.6 纹理系统

**stb_image 集成要点**：
```cpp
bool Texture2D::loadFromFile(const string& path, bool genMipmap) {
    stbi_set_flip_vertically_on_load(1);  // OpenGL Y 轴翻转
    unsigned char* data = stbi_load(path, &w, &h, &ch, 0);

    // ⚠️ 关键：修复 RGB(3通道) 纹理在非4倍宽下的崩溃
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // 原因：OpenGL 默认期望行宽为 4 字节对齐，
    // RGB 每像素 3 字节，非 4 倍数行宽会导致越界读取

    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    // 设置参数: CLAMP_TO_EDGE + LINEAR_MIPMAP_LINEAR / LINEAR

    stbi_image_free(data);  // 上传后立即释放 CPU 内存
    return true;
}
```

**TextureCache 单例**：
- 全局纹理缓存，跨状态持久化
- `load(path)` — 按路径缓存，避免重复加载
- `preloadRange(paths, start, end)` — 批量预加载
- `unloadDistant(paths, center, radius)` — 滑动窗口卸载策略

---

### 2.3 ImGui — 即时模式 UI

#### 2.3.1 为什么选择 ImGui 而非 Qt / CEGUI / 自研？

| 维度 | ImGui (Dear ImGui) | Qt5/6 | 自研 UI |
|------|-------------------|-------|---------|
| **集成成本** | 3 个文件，50 行初始化 | 需完整框架 | 数千行 |
| **渲染模式** | 即时模式（每帧重建） | 保留模式（状态树） | 自定义 |
| **GPU 后端** | 自动生成顶点数据 | 自绘 | 全手写 |
| **适合场景** | ✅ 工具/调试/游戏 UI | 桌面应用 | 特定需求 |
| **性能** | 轻量（< 1ms/帧） | 较重 | 取决于实现 |
| **调试价值** | 内置窗口/demo/指标 | 需额外工具 | 无 |
| **主题定制** | 全颜色可编程 | QSS 样式表 | 全手写 |

**选择理由**：
1. **即时模式 = 无状态同步**：UI 代码直接映射到渲染，没有 Model-View 绑定问题
2. **与 OpenGL 无缝集成**：ImGui 在游戏 OpenGL 渲染之上叠加绘制
3. **调试利器**：可直接在游戏中显示性能数据、变量值、调试窗口
4. **零运行时依赖**：3 个 .cpp + 3 个 .h 即可集成

#### 2.3.2 集成架构

```
初始化:
  ImGui::CreateContext()
  ImGui_ImplSDL2_InitForOpenGL(window, glContext)
  ImGui_ImplOpenGL3_Init("#version 330 core")
  io.IniFilename = nullptr     // 不保存窗口状态
  io.ConfigFlags |= NavEnableKeyboard | NavEnableGamepad
  Theme::apply()               // 深霓虹主题

每帧流程:
  UIManager::newFrame()
  ├─ ImGui_ImplOpenGL3_NewFrame()
  ├─ ImGui_ImplSDL2_NewFrame()
  └─ ImGui::NewFrame()

  [游戏渲染...]

  [ImGui 部件代码...]

  UIManager::renderFrame()
  ├─ ImGui::Render()
  └─ ImGui_ImplOpenGL3_RenderDrawData()

输入处理:
  ImGui_ImplSDL2_ProcessEvent(&event)  // 先于游戏逻辑
  → WantCaptureMouse / WantCaptureKeyboard 标志
  → 游戏根据标志决定是否处理输入
```

#### 2.3.3 关键设计决策

| 决策 | 原因 |
|------|------|
| `io.IniFilename = nullptr` | 游戏固定布局，不需要跨会话记住窗口位置 |
| `ImGuiCond_Always` 强制定位 | 避免窗口状态缓存导致的异常 |
| 全屏窗口用 `NoCollapse` | 防止用户意外关闭核心 UI |
| 深霓虹主题 (`#1a1a2e`) | 降低长时间游戏的眼疲劳 |
| 中文字体 `NotoSansSC` | 支持 CJK 字符，汉化友好 |
| Settings 侧边栏 cubic ease-out | 300ms 动画，视觉流畅 |
| 渲染顺序: MainMenu → Overlay → Sidebar | Sidebar 最后渲染 = 最上层 |

#### 2.3.4 ImGui vs OpenGL 原生渲染的职责划分

```
OpenGL 层（底层）:
  - 背景图片渲染
  - 游戏网格线
  - 音符（实例化渲染）
  - 粒子特效（点精灵）
  - HP 条（HUD）

ImGui 层（顶层）:
  - 菜单按钮/文字
  - 分数/连击显示
  - 设置面板
  - 场景过渡叠加层
  - 调试窗口
```

**为什么不全部用 ImGui？**
- 音符渲染需要高性能实例化，ImGui 不适合
- 粒子系统需要 GPU 点精灵，ImGui 无此能力

**为什么不全部用 OpenGL 原生？**
- 文字渲染需要字体光栅化（FreeType），ImGui 自带
- UI 布局（按钮、滑动条、下拉框）需要大量样板代码
- 组合使用是最佳实践

---

### 2.4 miniaudio — 音频引擎

#### 2.4.1 为什么选择 miniaudio 而非 FMOD / OpenAL / SDL_Audio？

| 维度 | miniaudio | FMOD | OpenAL-Soft | SDL_Audio |
|------|-----------|------|-------------|-----------|
| **许可** | ✅ MIT/公共域 | ❌ 商业收费 | ✅ LGPL | ✅ zlib |
| **集成** | 单头文件 (miniaudio.h) | SDK 下载 | 动态库 | 随 SDL2 |
| **功能** | 播放/混音/效果/设备枚举 | 全功能专业级 | 3D 空间音频 | 基础播放 |
| **延迟** | ✅ 低延迟（可配缓冲区） | 可配 | 中等 | 中等 |
| **解码** | 内置 stb_vorbis + dr_* | 全格式 | 需插件 | 有限 |

**选择理由**：
1. **零依赖**：单头文件，内置 MP3/WAV/FLAC/Vorbis 解码
2. **低延迟**：可配置缓冲区大小，适合节奏游戏
3. **MIT 许可**：无授权问题
4. **API 简洁**：`ma_engine_init()` + `ma_sound_init_from_file()` 即可播放

#### 2.4.2 音频架构

```
AudioEngine
├─ ma_engine*           // miniaudio 引擎实例
├─ ma_sound* 池         // 最多 2 路并发声音
│   ├─ ActiveSound[0]   // BGM / 预览
│   └─ ActiveSound[1]   // SFX / 第二路
├─ SFX 音效系统
│   ├─ SfxPool[4 类型][3 实例]  // Round-Robin 轮转
│   └─ SfxType: MenuClick, HitNormal, SliderTick, HitPerfect
└─ 音量管理
    ├─ masterVolume
    ├─ musicVolume / sfxVolume / previewVolume
    └─ calcVolume(type) = master × type
```

**预览循环机制**：
```
播放选歌预览:
  ma_sound_seek_to_pcm_frame(sound, startFrame)  // 跳转到起始点
  → 播放
  → 每帧检查: cursorMs >= previewEndMs？
  → 是 → seek 回 startFrame（无缝循环）
```

**为什么用 `std::atomic` 同步音频和游戏？**
- miniaudio 在独立线程中处理音频回调
- `Clock::syncFromAudio(audioFrameTimeMs)` 使用 `atomic::exchange` 获取音频位置
- 保证线程安全的同时避免互斥锁开销

---

## 三、架构设计

### 3.1 系统全景图

```
┌──────────────────────────────────────────────────────────────┐
│                        main.cpp                              │
│  Kernel::instance().init() → 注册状态 → run() → shutdown()   │
└──────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────┐
│                     Kernel（应用内核单例）                     │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────────┐   │
│  │ SDL_Window  │  │ StateManager │  │  EventManager     │   │
│  │ SDL_GLContext│  │  (状态机)    │  │  (事件总线)       │   │
│  └─────────────┘  └──────────────┘  └───────────────────┘   │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────────┐   │
│  │ Clock       │  │ Renderer     │  │  InputManager     │   │
│  │ (游戏时钟)   │  │ (OpenGL渲染) │  │  (SDL→语义事件)   │   │
│  └─────────────┘  └──────────────┘  └───────────────────┘   │
│  ┌─────────────┐  ┌──────────────┐                          │
│  │ UIManager   │  │ AudioEngine  │                          │
│  │ (ImGui后端) │  │ (miniaudio)  │                          │
│  └─────────────┘  └──────────────┘                          │
└──────────────────────────────────────────────────────────────┘
```

### 3.2 主循环设计 — 固定步长 + 累积器模式

```cpp
// kernel.cpp — 经典游戏循环模式
static constexpr double FIXED_DT = 1.0 / 240.0;  // ~4.17ms

void Kernel::run() {
    while (!m_shouldExit) {
        double frameTime = calculateFrameTime();           // 可变帧时间
        frameTime = min(frameTime, 0.25);                  // 防螺旋
        m_accumulator += frameTime;

        pumpInputEvents();                                 // 收集输入

        // 固定步长逻辑更新
        while (m_accumulator >= FIXED_DT) {
            m_stateManager.update(FIXED_DT);               // 状态逻辑
            m_audioEngine.update(FIXED_DT);                // 音频淡入淡出
            m_accumulator -= FIXED_DT;
        }

        // 渲染（使用插值时间，平滑显示）
        int64_t interpolatedTime = m_clock.interpolatedNowMs();
        m_renderer.renderFrame(interpolatedTime);          // OpenGL 渲染
        m_stateManager.render();                           // 状态渲染
        m_uiManager.renderFrame();                         // ImGui 渲染

        SDL_GL_SwapWindow(m_window);
    }
}
```

**为什么用固定步长而非可变步长？**

| 方式 | 优点 | 缺点 | 适合场景 |
|------|------|------|---------|
| **固定步长**（本项目） | 逻辑确定性（判定精确） | 需插值渲染 | 节奏游戏 |
| 可变步长 | 代码简单 | 逻辑不确定 | 休闲游戏 |

**渲染插值的价值**：
```
时间轴:  [更新0]          [更新1]          [更新2]
         0ms              4.17ms           8.34ms
                     ↑ 渲染时间=2ms
                     interpolatedTime = 更新0时间 + 进度×4.17ms
                     音符在屏幕上平滑移动，避免抖动
```

### 3.3 状态机设计

```
                    ┌──────────┐
                    │   Boot   │ (启动动画 + 预加载)
                    └────┬─────┘
                         │ 自动
                         ▼
 ┌──────────────┐   ┌──────────┐   ┌─────────────┐
 │   Settings   │◄──│ MainMenu │──►│ SongSelect  │
 │  (侧边栏)    │   └────┬─────┘   └──────┬──────┘
 └──────────────┘        │                │
                         │                │ 选歌
                         ▼                ▼
                    ┌─────────┐     ┌─────────┐
                    │  Exit   │     │ Playing │
                    └─────────┘     └────┬────┘
                                         │ ESC
                                         ▼
                                    ┌─────────┐
                                    │ Paused  │
                                    └────┬────┘
                                    QUIT  │  RESUME
                                         ▼
                                    ┌─────────┐
                                    │  Result │
                                    └─────────┘
```

**StateManager 关键设计**：
```cpp
class StateManager {
    GameState m_currentState = GameState::Count;  // 哨兵值！
    // 初始值必须是 Count 而非 Boot
    // 否则首次 transitionTo(Boot) 会因 newState == m_currentState 跳过 onEnter()

    void transitionTo(GameState newState) {
        if (newState == m_currentState) return;       // 去重
        m_transition.startFadeOut(0.4f);              // 400ms 淡出
        m_pendingState = newState;
    }

    void update(float dt) {
        m_transition.update(dt);
        if (m_transition.fadeOutComplete()) {
            // 切换状态
            m_states[m_currentState]->onExit();
            m_currentState = m_pendingState;
            m_states[m_currentState]->onEnter();
            m_transition.startFadeIn(0.3f);          // 300ms 淡入
        }
        m_states[m_currentState]->update(dt);
    }
};
```

**状态间数据传递**：
```cpp
// 跨状态类型安全的数据访问
auto* playing = m_stateManager.getStateAs<PlayingState>(GameState::Playing);
playing->getResultData();  // 获取分数/准确度 → 传给 ResultState
```

### 3.4 事件总线

```cpp
// 类型擦除事件总线 — 观察者模式
class EventManager {
    // 按 type_index 分派，每个事件类型对应一个订阅者列表
    unordered_map<type_index, vector<Subscription>> m_subscriptions;

    template<typename T>
    void emit(const T& event) {
        auto& subs = m_subscriptions[type_index(typeid(T))];
        for (auto& sub : subs) {
            sub.callback(any(event));  // std::any 类型擦除
        }
    }
};

// 注册顺序 = 调用顺序
// PlayingState 中: Combo → Score → HP → Effect → HUD
// 保证在连击更新后再计算分数，HP 最后变化
```

### 3.5 游戏时钟 — 音频驱动的权威时钟

```cpp
class Clock {
    atomic<int64_t> m_audioTimeMs{0};       // 音频播放位置（原子操作）
    steady_clock::time_point m_lastSync;    // 最后一次同步的系统时间

    void syncFromAudio(int64_t audioTimeMs) {
        // 仅在音频位置前进时更新（播放中）
        // atomic::exchange 保证线程安全
        if (audioTimeMs > m_audioTimeMs.load()) {
            m_audioTimeMs.store(audioTimeMs);
            m_lastSync = steady_clock::now();
        }
    }

    int64_t interpolatedNowMs() const {
        // 在两次音频同步之间，用系统时钟做微秒级插值
        // 消除音频回调粒度（~10ms）带来的画面抖动
        auto sinceSync = steady_clock::now() - m_lastSync;
        return m_audioTimeMs.load() + duration_cast<milliseconds>(sinceSync).count();
    }
};
```

**为什么用音频位置而非帧时间做判定？**
- 音频是用户感知节奏的基准
- 帧率波动不影响判定精度
- 消除音频/视频不同步

---

## 四、功能模块详解

### 4.1 谱面系统

#### 4.1.1 双格式解析器架构

```
                  BeatmapParser（抽象接口）
                  ├── parse(content, builder)
                  └── formatName()
                         │
          ┌──────────────┴──────────────┐
          ▼                              ▼
    MmaParser                      OsuParser
    (.mma 原生格式)                (.osu osu!格式)
    ├─ MMA1 版本验证               ├─ osu file format v14
    ├─ 5 段落解析                  ├─ HitCircle → Tap
    ├─ Key=Value 二元分隔          ├─ Slider → Hold
    └─ 直接输出 Note               ├─ Spinner → 中心 Tap
                                   └─ 动态呼吸矩阵生成
```

#### 4.1.2 MmaParser — 原生格式

```
MMA1                          ← 版本标识
[General]
audio_file = song.mp3
background = bg.jpg
preview_time = 10000

[Difficulty]
hp = 8
od = 7
ar = 6
star = 4.5

[Meta]
title = My Song
artist = Composer
creator = Mapper
version = Expert

[Formations]
0,3,4                         ← time,rows,cols
15000,4,4,fade,300,0.9,scale  ← 完整参数

[Notes]
1000,0,1,T                    ← time,row,col,type
2500,1,2,H,5000               ← hold: startTime,row,col,H,endTime
```

#### 4.1.3 OsuParser — osu! 适配器 + 动态呼吸矩阵

**核心算法 — 滑动窗口空间分析**：
```
输入: osu! 谱面中所有 Note 的 (x, y, time)
输出: Formation 序列 (rows, cols, time)

算法步骤:
1. 时间分桶: 2s 滑动窗口，500ms 步进
2. 坐标归一化: osu(512,384) → playfield(0~1, 0~1)
3. 空间分布:
   - spread = 到 playfield 中心的归一化距离 × 密度因子
   - 密度因子 = 窗口内 note 数 / 平均 note 数
4. 网格尺寸映射: spread ∈ [0.1, 0.8] → 行列 ∈ [3, 8]
5. 迟滞( hysteresis ): 只有变化 > 阈值才切换
6. 合并连续相同尺寸
7. 去重

效果: 音符密集密集段 → 大网格(8×8)；稀疏段 → 小网格(3×3)
      视觉上"呼吸"随着音乐节奏变化
```

**坐标映射细节**：
```cpp
// osu 坐标 512×384 → 网格 (row, col)
float normX = (x - 512.0f) / 512.0f;   // [-1, 1]
float normY = (y - 384.0f) / 384.0f;   // [-1, 1]
int col = (normX + 1.0f) / 2.0f * m_cols;  // [0, cols)
int row = (normY + 1.0f) / 2.0f * m_rows;  // [0, rows)
```

**Slider 转换**：
```cpp
// 按 1/4 拍间隔生成 Tap 序列
for (float t = startTime; t <= endTime; t += msPerBeat / 4) {
    float progress = (t - startTime) / duration;
    // 沿曲线控制点线性插值位置
    Vec2 pos = lerpAlongCurve(controlPoints, progress);
    // ⚠️ 关键：每个 Tap 用自身时间查找 Formation
    Formation fmt = findFormationAt(t);
    auto [row, col] = gridMapping(pos, fmt);
    createTap(t, row, col);
}
```

#### 4.1.4 导入流程

```
IMPORT 按钮
  └─ Win32 GetOpenFileNameW（选择 .osz）
      └─ 文件扩展名检查（.osz → .osz）
          └─ ZIP 解压到临时目录（PowerShell Expand-Archive）
              └─ 递归查找 .osu 文件
                  └─ 对每个 .osu 文件:
                      ├─ OsuParser::parse()
                      ├─ BeatmapBuilder::build() → 8 条验证
                      ├─ SHA256 计算
                      ├─ SHA256 去重检查
                      ├─ MmaSerializer::serialize() → .mma
                      ├─ 复制音频 + 背景图
                      └─ 清理临时文件
```

### 4.2 判定系统

#### 4.2.1 策略模式 — 可插拔判定

```cpp
class IJudgeStrategy {
    virtual int32_t perfectWindow(float od) const = 0;
    virtual int32_t goodWindow(float od) const = 0;
    virtual int64_t missThreshold(float od) const = 0;
};

class StandardJudgeStrategy : public IJudgeStrategy {
    // osu!mania 公式
    int32_t perfectWindow(float od) const override {
        return 22.0f - 1.05f * od;  // OD=0→±22ms, OD=10→±11.5ms
    }
    int32_t goodWindow(float od) const override {
        return 65.0f - 2.6f * od;   // OD=0→±65ms, OD=10→±39ms
    }
};

class EasyJudgeStrategy : public IJudgeStrategy {
    // Easy 模组: 窗口 ×1.4
    int32_t perfectWindow(float od) const override {
        return static_cast<int32_t>(StandardJudgeStrategy().perfectWindow(od) * 1.4f);
    }
};
```

#### 4.2.2 多列独立判定队列

```cpp
class JudgeQueue {
    static constexpr int MAX_COLS = 8;
    array<ColumnQueue, MAX_COLS> m_columns;

    struct ColumnQueue {
        vector<Note> notes;  // 时间排序
        size_t head = 0;     // 当前队头
    };

    // 按键判定
    void onKeyPress(int64_t pressTime, int column, float od) {
        auto& col = m_columns[column];
        Note& head = col.notes[col.head];
        int64_t delta = abs(pressTime - head.time);

        if (delta <= perfectWindow(od)) → emit Hit(Perfect);
        else if (delta <= goodWindow(od)) → emit Hit(Good);
        else if (delta <= missThreshold(od)) → emit Miss;
        else → Ignored; // 不消耗 note
    }

    // Hold 释放判定
    void onKeyRelease(int64_t releaseTime, int column, float od) {
        Note& hold = getActiveHold(column);
        int64_t delta = abs(releaseTime - hold.holdEnd);
        // 与 Press 相同的窗口判定
    }
};
```

### 4.3 HP & 分数系统

#### HP 管理
```
HP ∈ [0, 1]

判定影响:
  Perfect → +0.02
  Good    → +0.01
  Miss    → -0.04

被动消耗:
  drainPerFrame(dt) → hp -= hpDrain × dt / 100.0

模组系统 (IHpMod):
  NoFailMod → isDead() 始终返回 false
```

#### 分数公式
```cpp
scoreForHit = BASE_SCORE(300) × accuracyBonus × comboMultiplier;

accuracyBonus:
  Perfect = 1.00
  Good    = 0.6667
  Miss    = 0.3333

comboMultiplier:
  multiplier = 1.0 + log2(combo + 1) × 0.2
  // combo=0 → ×1.0
  // combo=15 → ×1.8
  // combo=255 → ×2.6
  // 对数增长，防止高连击时分数膨胀
```

### 4.4 阵型过渡系统

**三种过渡类型**：

| 类型 | 视觉效果 | 数学描述 |
|------|---------|---------|
| Fade | 旧阵型淡出 + 新阵型淡入 | alpha 交叉混合 |
| Insert | 新格子从边缘插入 | 位置从边界 lerp 到目标 |
| Rotate | 矩阵旋转变换 | 绕中心旋转 |

**过渡缓动**：
```cpp
// easeInOutCubic — 平滑起止，中间加速
float t = clamp(elapsed / duration, 0.0f, 1.0f);
float progress = (t < 0.5f)
    ? 4.0f * t * t * t           // ease-in
    : 1.0f - pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;  // ease-out
```

### 4.5 资源管理

#### 对象池（NotePool）
```cpp
class NotePool {
    vector<Note> m_storage;      // 连续内存，预分配
    vector<size_t> m_freeList;   // 空闲索引栈

    Note* acquire() {
        if (m_freeList.empty()) return nullptr;
        size_t idx = m_freeList.back();
        m_freeList.pop_back();
        return &m_storage[idx];  // O(1)
    }

    void release(Note* note) {
        note->reset();
        m_freeList.push_back(note - m_storage.data());  // O(1)
    }
};
```

**为什么用对象池而非 new/delete？**
- 游戏中大量 Note 需要频繁创建/销毁
- 对象池消除堆分配开销
- 连续内存布局，缓存友好

#### 异步资源加载
```cpp
class AssetLoader {
    thread m_worker;                   // 后台加载线程
    queue<LoadTask> m_pending;         // 待加载队列
    mutex m_mutex;                     // 线程安全
    atomic<float> m_progress{0};       // 加载进度

    // BootState 中预加载:
    // - 默认背景图
    // - 默认音频
    // - 字体文件
    // - UI 纹理
};
```

---

## 五、技术决策对比分析

### 5.1 SDL2 vs GLFW vs 裸 Win32 API

| 维度 | SDL2（采用） | GLFW | 裸 Win32 API |
|------|-------------|------|-------------|
| 窗口创建 | 2 行代码 | 3 行代码 | 200+ 行 |
| OpenGL 上下文 | `SDL_GL_CreateContext` | `glfwMakeContextCurrent` | `wglCreateContext` |
| 输入处理 | 统一事件模型 | 回调 + 轮询 | `GetMessage` + `PeekMessage` |
| 手柄支持 | ✅ 内置 | ✅ 内置 | ❌ 需要 XInput |
| DPI 感知 | `SDL_HINT` 一行 | `GLFW_SCALE_TO_MONITOR` | 处理 `WM_DPICHANGED` |
| 音频 | ✅（项目未用） | ❌ | ❌ |
| 学习成本 | 低 | 低 | 高 |

**结论**: SDL2 是最平衡的选择，功能全面且学习成本低。

### 5.2 即时模式 (ImGui) vs 保留模式 (Qt)

| 维度 | ImGui（采用） | Qt Widgets / QML |
|------|-------------|-----------------|
| UI 构建 | 每帧重建（`ImGui::Button("Play")`） | 状态树 + 信号/槽 |
| 状态管理 | 无（代码 = UI 状态） | 显式管理 widget 状态 |
| 与其他 OpenGL 代码协作 | ✅ 在同管线中渲染 | ❌ 需要特殊集成 |
| 二进制大小 | 小 | 大 |
| 主题 | 全颜色可编程 | QSS / 自定义 |
| 运行时性能 | 极轻量 | 较重 |
| 调试特性 | `ImGui::ShowDemoWindow` | 需要 Qt Creator |

**即时模式的哲学**：
```cpp
// ImGui：UI 声明即渲染，无状态同步
if (ImGui::Button("Play")) { ... }
if (ImGui::SliderFloat("Volume", &vol, 0, 1)) { ... }

// Qt：需要信号/槽连接
connect(playButton, &QPushButton::clicked, this, &MainWindow::onPlay);
connect(volumeSlider, &QSlider::valueChanged, this, &MainWindow::onVolumeChanged);
```

### 5.3 OpenGL 3.3 Core vs Vulkan

| 维度 | OpenGL 3.3（采用） | Vulkan |
|------|-------------------|--------|
| 初始化代码量 | ~50 行 | ~1000 行 |
| Draw Call 开销 | ~5-10 μs | ~1-2 μs |
| 内存管理 | 驱动自动 | 手动 VkDeviceMemory |
| 多线程 | OpenGL 单线程 | 原生命令缓冲多线程 |
| 适用项目 | 2D/轻量 3D | AAA 大作/复杂场景 |
| 工具链 | RenderDoc | 验证层 + RenderDoc |

**结论**: 本项目 draw call 极少（< 10/帧），OpenGL 的驱动开销完全可以忽略。Vulkan 的显式同步（管线屏障）、描述符集、内存分配器会带来巨大认知负担而无实际收益。

### 5.4 miniaudio vs FMOD / OpenAL

| 功能 | miniaudio（采用） | FMOD | OpenAL |
|------|------------------|------|--------|
| 播放/停止/音量 | ✅ | ✅ | ✅ |
| Seek（跳转） | ✅ 采样级 | ✅ | ✅ |
| 多路混音 | ✅ | ✅ | ✅ |
| 3D 空间音频 | ❌ 不需要 | ✅ | ✅ |
| DSP 效果 | ✅ 有限 | ✅ 丰富 | ❌ |
| 许可 | MIT | 商业收费 | LGPL |
| 集成 | 单头文件 | SDK + DLL | 动态库 |

### 5.5 内存管理：对象池 vs new/delete vs 智能指针

| 方案 | 分配/释放 | 内存碎片 | 缓存局部性 | 适用 |
|------|----------|---------|-----------|------|
| NotePool（采用） | O(1) 栈操作 | 无 | 最优（连续） | 高频创建/销毁 |
| new/delete | ~50ns | 可能 | 随机 | 低频 |
| unique_ptr | new/delete + 安全 | 可能 | 随机 | 唯一所有权 |
| shared_ptr | new + 引用计数 | 可能 | 随机 | 共享所有权 |

**本项目策略**：
- Note → NotePool（高频，连续内存）
- Shader/Texture → unique_ptr + 移动语义（RAII）
- IHpMod → shared_ptr（共享所有权）

---

## 六、性能优化策略

### 6.1 渲染优化

| 优化项 | 实现方式 | 效果 |
|--------|---------|------|
| 实例化渲染 | 1 次 draw call 渲染所有音符 | 消除 CPU draw call 瓶颈 |
| 每帧 VBO 上传 | `glBufferData`（非 SubData） | 简单可靠，实例 < 1024 无性能差 |
| 纹理缓存 | TextureCache 单例 | 避免重复 stb_image 解码 |
| 多纹理层绑定 | 6 个 sampler 常驻（unit 0-5） | 无状态切换开销 |
| Alpha 裁剪 | 粒子 `discard` 圆形裁剪 | GPU 原生，零额外 draw call |

### 6.2 逻辑优化

| 优化项 | 实现方式 |
|--------|---------|
| 固定步长 | 240Hz 逻辑更新，消除浮点累积误差 |
| 音频驱动时钟 | `std::atomic` 无锁同步，消除判定漂移 |
| 对象池 | O(1) acquire/release，无堆分配 |
| 滑动窗口卸载 | TextureCache 智能卸载远离当前页的资源 |

### 6.3 内存布局

```
NotePool:
│ Note[0] │ Note[1] │ Note[2] │ ... │ Note[N-1] │
└──────────────── 连续内存 ─────────────────────┘
  → Cache Line 友好，预取命中率高

FreeList:
│ 3 │ 7 │ 1 │ ... │  // 栈式操作，push/pop O(1)
```

---

## 七、设计模式全景图

| 模式 | 应用位置 | 解决的问题 |
|------|---------|-----------|
| **单例** | Kernel, FallbackShader, TextureCache | 全局唯一实例，统一访问点 |
| **状态模式** | StateManager + 6 个 GameState | 游戏流程控制，状态间解耦 |
| **观察者** | EventManager（事件总线） | 模块间松耦合通信 |
| **策略模式** | IJudgeStrategy, IHpMod | 可插拔判定规则和 HP 行为 |
| **工厂方法** | Shader::compile(), NoteFactory | 复杂对象创建，错误处理 |
| **建造者** | BeatmapBuilder | 分步构建复杂对象 + 集中验证 |
| **适配器** | OsuParser | osu! 格式 → 内部 Beatmap 格式 |
| **外观模式** | UIManager, AudioEngine | 封装第三方库的复杂接口 |
| **RAII** | Shader, Texture2D | GL 资源自动生命周期管理 |
| **对象池** | NotePool | 高频对象复用，消除分配开销 |
| **结果类型** | Result<T> | 无异常错误传播 |
| **模板方法** | GameStateBase (onEnter/update/render/onExit) | 定义算法骨架，子类实现细节 |

---

## 八、面试常见问题与回答

### Q1: 你是如何处理高 DPI 显示的？

> 我采用了 **Per-Monitor V2 DPI 感知模式**。在 `SDL_Init()` 之前调用 `SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2")`，这样 SDL 和 Windows 直接以物理像素为单位进行所有坐标 API 调用。这意味着我不需要在代码中维护 `dpiScale` 变量，也不需要做逻辑像素↔物理像素的转换。`glViewport` 使用 `SDL_GL_GetDrawableSize()` 获取物理像素尺寸，确保在任何缩放率下都清晰锐利。

### Q2: 你的 Shader 系统最重要的设计考量是什么？

> **错误处理**。着色器编译/链接失败是预期中的可恢复错误，不应该让整个游戏崩溃。我设计了三级降级方案：
> 1. 自定义 Shader 编译成功 → 正常使用
> 2. 编译/链接失败 → `Result<Shader>` 传播错误，上层记录日志
> 3. 全部失败 → FallbackShader 单例提供纯色着色器，游戏至少能运行
>
> 同时，在编译失败时，我会先 `glDeleteShader()` 清理 GL 资源再返回错误，确保不会泄漏 OpenGL 对象。

### Q3: 你的实例化渲染方案是怎样的？为什么要这样设计？

> 在 NoteRenderer 中，我使用 **单次 draw call 绘制所有类型元素**：
> - 1 个共享四边形 VBO（6 个顶点）
> - 3 个实例 VBO（位置、颜色、纹理层 ID）
> - 1 次 `glDrawArraysInstanced(GL_TRIANGLES, 0, 6, N)` 绘制 N 个实例
>
> 在片段着色器中通过 `vTexLayer` 选择 6 个绑定纹理中的 1 个。这样可以在单次 draw call 中同时渲染 block 背景、tap 音符、slider 音符、overlay 判定环、slider 进度环 5 种不同的元素类型。对于节奏游戏 ≤ 256 个可见音符的场景，这从根本上消除了 CPU 端的 draw call 瓶颈。

### Q4: 你是如何集成 ImGui 的？遇到什么问题？

> 我封装了一个 **UIManager 外观类**，管理 ImGui 的完整生命周期：`init() → newFrame() → [widget] → renderFrame() → shutdown()`。
>
> 遇到的关键问题是 **ImGui 窗口状态缓存**：ImGui 默认会保存窗口大小/位置到 `imgui.ini`，导致下次启动时窗口显示异常。解决方案是设置 `io.IniFilename = nullptr` 禁用状态持久化，并使用 `ImGuiCond_Always` 强制定位。
>
> 另一个问题是 **Settings 侧边栏的输入处理**：需要在 sidebar 窗口上渲染一个透明的 overlay 窗口来遮挡背景，但 overlay 不能接收输入（使用 `NoInputs` 标志），点击外部关闭的逻辑统一在 `render()` 末尾处理。

### Q5: 你是如何实现游戏时钟的？为什么不用帧时间直接驱动？

> 我使用了 **音频播放位置作为权威时钟源**，通过 `Clock::syncFromAudio()` 接收 miniaudio 的播放位置（`std::atomic<int64_t>` 保证线程安全）。在两次音频同步之间（~10ms 间隔），使用 `steady_clock` 做微秒级插值，实现 `interpolatedNowMs()` 平滑渲染。
>
> 不使用帧时间的原因是：帧率波动（60fps → 144fps → 偶尔掉帧）会导致判定窗口偏移。音频是用户感知节奏的基准，以它为准才能保证判定精度。

### Q6: 你的项目使用了哪些 C++17 特性？

> - `std::variant` — Result<T> 区分联合的错误处理
> - `std::optional` — 可选值表达
> - `std::string_view` — 零拷贝字符串处理
> - `std::any` — EventManager 类型擦除
> - 结构化绑定 — 解构 pair/tuple
> - `if constexpr` — 编译期条件分支
> - `std::filesystem` — 跨平台文件系统操作
> - Inline 变量 — 头文件中定义静态成员
> - `[[nodiscard]]` — 强制检查返回值

### Q7: 项目中最棘手的技术问题是什么？

> 有两个：
>
> 1. **GL_UNPACK_ALIGNMENT 导致纹理加载崩溃**：OpenGL 默认要求像素行 4 字节对齐，而 RGB（3 通道）纹理在非 4 倍数宽度时会越界读取。表现为程序无日志直接段错误。解决方案是 `glPixelStorei(GL_UNPACK_ALIGNMENT, 1)`。
>
> 2. **OsuParser 动态呼吸矩阵中 Formation 查找时机错误**：Slider 转为 Tap 序列时，所有 Tap 都用了 Slider 起始时间的 Formation，导致跨 Formation 边界时坐标越界（OOB）。修复方式是每个 Tap 用 `findFormationAt(tap.time)` 而非 `findFormationAt(slider.startTime)`。

---

## 附录

### A. 项目文件结构

```
Melody-Matrix/
├── CMakeLists.txt              # CMake 构建配置（192行）
├── src/
│   ├── main.cpp                # 程序入口
│   ├── version.h.in            # 版本号模板
│   ├── core/                   # 核心架构（6 状态 + Kernel + Clock）
│   ├── audio/                  # 音频引擎（miniaudio 封装）
│   ├── input/                  # 输入管理（SDL→语义事件）
│   ├── beatmap/                # 谱面系统（Parser/Builder/Serializer/Pool）
│   ├── gameplay/               # 游戏逻辑（Judge/HP/Combo/Score/Formation）
│   ├── renderer/               # OpenGL 渲染（Note/Border/Effect/HUD/Shader/Texture）
│   ├── ui/                     # ImGui UI（UIManager/Theme）
│   ├── platform/               # 平台适配（Config/FileDialog/Zip/FileSystem）
│   └── util/                   # 工具库（Logger/EventManager/Result/Hash/Error）
├── tests/                      # 单元测试
├── third_party/                # 6 个 vendored 第三方库
├── assets/                     # 游戏资源
└── docs/                       # 设计文档
```

### B. 核心技术决策速查表

| 需求 | 方案A | 方案B | 选择 | 理由 |
|------|-------|-------|------|------|
| 窗口管理 | SDL2 | GLFW | SDL2 | 功能更全面 |
| 图形 API | OpenGL 3.3 | Vulkan | OpenGL | 项目复杂度匹配 |
| UI 框架 | ImGui | Qt | ImGui | 即时模式 + 调试能力 |
| 音频引擎 | miniaudio | FMOD | miniaudio | MIT许可 + 零依赖 |
| 纹理加载 | stb_image | FreeImage | stb_image | 单头文件 |
| 错误处理 | Result<T> | 异常 | Result<T> | 可恢复错误不应用异常 |
| 对象分配 | 对象池 | new/delete | 对象池 | 高频创建场景 |
| 游戏时钟 | 音频驱动 | 帧驱动 | 音频驱动 | 判定精度 |
| DPI 方案 | Per-Monitor V2 | 手动缩放 | Per-Monitor V2 | OS 原生方案 |
