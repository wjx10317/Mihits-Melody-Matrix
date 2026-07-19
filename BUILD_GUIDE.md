# Melody Matrix 构建指南

## 已修复的问题

### 1. `renderer.h` - 不完整类型错误（编译错误）
**文件**: `src/renderer/renderer.h`
**问题**: `std::unique_ptr<NoteRenderer>` 需要完整的 `NoteRenderer` 类型定义
**修复**: 
- 添加了 `#include "renderer/note_renderer.h"`
- 删除了前向声明 `class NoteRenderer;`

### 2. `hp_manager.h` - 未引用形参警告
**文件**: `src/gameplay/hp_manager.h`
**问题**: 形参 `result` 未使用
**修复**: 将 `JudgmentResult result` 改为 `JudgmentResult /*result*/`

---

## 构建步骤

### 方法1：使用 Visual Studio Developer Command Prompt（推荐）

1. **打开 Visual Studio Developer Command Prompt**:
   - 开始菜单 → Visual Studio 2022 → **Developer Command Prompt for VS 2022**

2. **进入仓库根目录后执行**（任意解压/克隆路径均可）:
   ```batch
   cd /d <本仓库根目录>
   if exist build-ninja rmdir /s /q build-ninja
   mkdir build-ninja
   cd build-ninja
   cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Debug ..
   ninja
   ```

3. **等待编译完成**（约 2-5 分钟）

4. **验证输出**:
   ```
   <仓库根>\build-ninja\melody_matrix.exe
   ```
   （VS 生成器则为 `build-vs\bin\Debug\melody_matrix.exe`）

---

### 方法2：使用提供的批处理脚本

1. **在仓库根目录运行**（脚本已按自身路径定位，勿依赖旧的 `D:\colin\...` 硬编码）:
   - `build_vs.bat` / `build_simple.bat`（Visual Studio 生成器）
   - `build_now.bat` / `build_complete.bat`（Ninja）

2. **双击或在终端中运行脚本**（需已安装 VS 2022）

3. **等待构建完成**

> 注意：GitHub 下载的是**源码**，不是可执行包；需先构建。谱面已不进仓库，请在游戏内导入 `.osz`。

---

## 预期结果

### ✅ 构建成功
- 输出文件: `<仓库根>\build-ninja\melody_matrix.exe`（或 `build-vs\bin\Debug\...`）
- 同目录应有 `SDL2.dll`、`assets\`、`res\`
- 无编译错误；可能有少量警告（可忽略）

### ❌ 构建失败
如果构建失败，请：
1. 复制完整的错误信息
2. 发给我，我会继续修复

---

## 常见问题

### Q: 下载 ZIP 后没有 .exe？
**A**: 仓库是源码。先跑 `build_vs.bat`（或按上面步骤 CMake 构建），再运行生成目录里的 `melody_matrix.exe`。

### Q: 构建脚本找不到工程？
**A**: 旧脚本曾写死 `D:\colin\Melody-Matrix`。请使用仓库内已修复的 bat（以 `%~dp0` 定位），或先 `cd` 到你解压后的根目录。

### Q: 能启动但没有歌？
**A**: `assets/beatmaps` 不随仓库分发，请在主菜单导入 `.osz`。

### Q: CMake 找不到 Ninja？
**A**: 确保使用 Visual Studio Developer Command Prompt，它会自动设置 Ninja 路径；或改用 `build_vs.bat`。

### Q: 编译失败，提示 "cannot open input file"？
**A**: 可能是文件被锁定。关闭所有 IDE 和编辑器，然后重新构建。

### Q: 编译很慢？
**A**: 首次构建需要 2-5 分钟。后续构建会快很多（增量编译）。

---

## 下一步

构建成功后，您可以：
1. 运行游戏: `cd /d <仓库根>\build-vs\bin\Debug`（或 `build-ninja`）后执行 `melody_matrix.exe`
2. 导入谱面后再游玩
3. 如果有运行时错误，请告诉我具体报错或日志
