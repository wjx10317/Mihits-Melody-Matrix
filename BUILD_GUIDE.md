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

2. **执行以下命令**:
   ```batch
   cd D:\colin\Melody-Matrix
   if exist build-ninja rmdir /s /q build-ninja
   mkdir build-ninja
   cd build-ninja
   cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Debug ..
   ninja
   ```

3. **等待编译完成**（约 2-5 分钟）

4. **验证输出**:
   ```
   D:\colin\Melody-Matrix\build-ninja\melody_matrix.exe
   ```

---

### 方法2：使用提供的批处理脚本

1. **打开脚本文件**:
   - 文件位置: `D:\colin\Melody-Matrix\build_complete.bat`

2. **右键点击脚本** → **以管理员身份运行**

3. **等待构建完成**

---

## 预期结果

### ✅ 构建成功
- 输出文件: `D:\colin\Melody-Matrix\build-ninja\melody_matrix.exe`
- 无编译错误
- 可能有少量警告（可忽略）

### ❌ 构建失败
如果构建失败，请：
1. 复制完整的错误信息
2. 发给我，我会继续修复

---

## 常见问题

### Q: CMake 找不到 Ninja？
**A**: 确保使用 Visual Studio Developer Command Prompt，它会自动设置 Ninja 路径。

### Q: 编译失败，提示 "cannot open input file"？
**A**: 可能是文件被锁定。关闭所有 IDE 和编辑器，然后重新构建。

### Q: 编译很慢？
**A**: 首次构建需要 2-5 分钟。后续构建会快很多（增量编译）。

---

## 下一步

构建成功后，您可以：
1. 运行游戏: `cd D:\colin\Melody-Matrix\build-ninja && melody_matrix.exe`
2. 如果有运行时错误，请告诉我，我会继续修复
