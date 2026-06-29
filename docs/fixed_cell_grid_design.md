# 固定格宽 + 谱面缩放（blockSize）渲染方案

> 状态：**设计稿**（已确认关键决策，待实现）  
> 目标：所有 item 纹理固定为 **256×256**；逻辑格使用**固定默认像素尺寸**，与矩阵行列数解耦；`background.png` 底格满格平铺以保持黑边相连，谱面 `blockSize` 仅控制格内 note/overlay/slider 等内容缩放；只在 `SCALE_ONLY` / blockSize-only 变换时**判定不中断**。

---

## 1. 背景与问题

### 1.1 当前行为

格宽由**屏幕可用区域 ÷ 当前行列数**动态计算：

```cpp
const float gw = (W - 2 * margin) / cols;   // 例：6 列 → 280px，3 列 → 560px
const float gh = (H - 2 * margin) / rows;
const float cellW = gw * blockSize;
```

| 问题 | 表现 |
|------|------|
| 格宽随矩阵变大而缩小 | 6×4 活跃 4 列时单格仅 ~280px，视觉拥挤 |
| 格宽随矩阵变小而放大 | 3×2 单格 ~560px，`blockSize=0.9` 缝隙极明显 |
| `blockSize` 与格宽耦合 | 铺师想用 0.9 做「快打留白」，大矩阵下效果失真 |
| 临时补丁 `effectiveBlockSize` | 按像素缝隙上限回填，无法表达「矩阵不变、仅内容缩放」 |

### 1.2 期望行为

1. **默认格宽/格高固定**（与 `rows`/`cols` 无关），始终为 item 默认像素尺寸 `256×256` px。
2. **矩阵总尺寸** = `cols × gw` × `rows × gh`；行列变多 → 矩阵在屏幕上变大（可滚动），行列变少 → 矩阵变小。
3. **所有 item 图片资源固定为 256×256**，运行时不通过替换资源尺寸表达大小差异。
4. **谱面 `blockSize`** 在固定格子内二次缩放内容 item（tap / slider / overlay / sliderpush 同步）；`background.png` 始终铺满逻辑格。
5. **`SCALE_ONLY` / blockSize-only 变换**：矩阵外框像素尺寸不变，格内 item 变大/变小 —— 「矩阵不变，大小变」。
6. **判定与视觉解耦**：`blockSize`、过渡动画**不影响** OD 窗口；阵型变换期间只放开 `SCALE_ONLY` / blockSize-only 的判定（见 §6）。

---

## 2. 核心概念

```
┌─────────────── 逻辑格 (固定 gw × gh) ───────────────┐
│  margin (格内)                                      │
│ background.png 满格铺底，黑边与相邻格相连        │
│    ┌──────── item 区 (gw×blockSize) ───────┐       │
│    │  tap / slider / overlay / sliderpush  │       │
│    └───────────────────────────────────────┘       │
└────────────────────────────────────────────────────┘
```

| 名称 | 来源 | 作用域 | 是否影响判定 |
|------|------|--------|--------------|
| `gw`, `gh` | 引擎常量（默认像素格宽） | 布局、滚动、note 定位 | **否** |
| `rows`, `cols` | `.mma` Formation | 逻辑网格、按键映射列索引 | **否**（列索引不变） |
| `blockSize` | `.mma` Formation | 格内内容 item 缩放、过渡插值 | **否** |
| `transformType` | `.mma` Formation | 切换/滑入/旋转/缩放动画 | **否**（仅视觉） |
| OD / timing | 难度与配置 | Perfect/Good/Miss 窗口 | **是** |

**判定单位**始终是 `(col, row, time)`，与像素无关 —— 现有 `JudgeQueue` 已满足，无需改判定窗口公式。

---

## 3. 默认格宽取值

### 3.1 已确认基准

采用与 item 资源一致的**方格逻辑格**：

```cpp
constexpr float kDefaultCellW = 256.f;
constexpr float kDefaultCellH = 256.f;
```

所有 item 贴图（`background.png`、`tap.png`、`slider.png`、`overlay.png`、`sliderpush_*`）按 **256×256** 资源管理；`background.png` 永远按 256px 满格绘制，其他内容 item 在 `blockSize=1.0` 时按 256px 原始逻辑尺寸渲染，运行时只按 `blockSize` 改变内容 item 尺寸。

### 3.2 垂直溢出策略

设计上矩阵不应高于屏幕；如果谱面或未来配置导致 `matrixH > screenH` 并存在崩溃风险，采用**裁剪**，不做整体缩小。

原因：整体缩小会让「固定格宽」重新变成动态缩放，视觉逻辑形成回环，不便调试与阅读。

### 3.3 可配置（可选，二期）

```ini
[video]
default_cell_w = 256
default_cell_h = 256
```

默认写死在 `grid_layout.h`，首版不放进 `config.ini`，减少变量。

---

## 4. 布局模型

### 4.1 新增模块 `GridLayout`

集中计算，消除 `note_renderer` / `renderer` / `playing_state` / `border_renderer` 多处重复公式。

```cpp
// src/renderer/grid_layout.h（或 src/gameplay/grid_layout.h）

struct GridLayout {
    static constexpr float kScreenW = 1920.f;
    static constexpr float kScreenH = 1080.f;
    static constexpr float kMargin  = 120.f;
    static constexpr float kDefaultCellW = 256.f;
    static constexpr float kDefaultCellH = 256.f;

    int rows = 0, cols = 0;
    float blockSize = 1.f;

    float gw() const { return kDefaultCellW; }
    float gh() const { return kDefaultCellH; }
    float contentW() const { return gw() * blockSize; }
    float contentH() const { return gh() * blockSize; }

    float matrixW() const { return cols * gw(); }
    float matrixH() const { return rows * gh(); }

    /// 逻辑格 (r,c) 中心点屏幕坐标（含 scrollOffset）
    void cellCenter(int r, int c,
                    int32_t activeStartCol, int32_t activeEndCol,
                    float scrollOffset,
                    float& outX, float& outY) const;

    /// 活跃窗口水平范围 [left, right]（用于高亮/渐隐）
    void activeBandX(int32_t activeStartCol, int32_t activeEndCol,
                     float scrollOffset,
                     float& outLeft, float& outRight) const;
};
```

**原则**：全项目只通过 `GridLayout` 取 `gw/gh/坐标`，禁止再写 `(W-2*margin)/cols`。

### 4.2 矩阵在屏幕上的锚点

| 轴 | 规则 |
|----|------|
| **水平** | 以**活跃列窗口**居中（与现逻辑一致）：活跃 4 列位于屏幕中央，`scrollOffset` 平移整矩阵 |
| **垂直** | 矩阵**底边**对齐 `y = H - margin`（note 从底部起排，与现 `note_renderer` 一致） |

```
屏幕
┌────────────────────────────────────┐
│         （谱面背景全屏）              │
│                                    │
│      ┌── 6 列 × 256 = 1536px ──┐   │  ← 宽于活跃窗口时靠滚动显示中央 4 列
│      │ ■ ■ ■ ■ ■ ■              │   │
│      │ ■ ■ ■ ■ ■ ■              │   │
│      └──────────────────────────┘   │  ← 底边 = H - margin
└────────────────────────────────────┘
```

### 4.3 溢出与滚动

- `matrixW > playW`：沿用现有 `ScrollWindow` + `scrollOffset`；**仅 `scrollOffset` 计算改用固定 `gw`**。
- `matrixH > playH`：原则上不会出现；若出现可能引发崩溃的边界情况，**直接裁剪**。
- 列数 ≤ 4：矩阵水平居中于 play 区域（活跃列 = 全部列）。

### 4.4 blockSize 语义（更新后）

```cpp
// .mma Formation.blockSize — 文档更新
/// 格内内容 item 缩放（1.0=贴满逻辑格，0.9=四周留 10% 缝，1.1=超出逻辑格）
/// background.png 不受此值影响，始终满格铺底
/// 逻辑格尺寸固定，与 rows/cols 无关
float blockSize = 1.0f;
```

**删除** `NoteRenderer::effectiveBlockSize()` 像素缝隙上限逻辑（新模型下不再需要）。

过渡时 `blockSize` 插值：

```cpp
float interp = lerp(prev.blockSize, next.blockSize, ease(t));
layout.blockSize = interp;  // SCALE_ONLY 与瞬间切换均走同一套
```

### 4.5 parser 默认值

`osu_parser` 非初始 formation 的默认 `blockSize` 改为 **1.0**。

```cpp
static constexpr double kDefaultBlockSize = 1.0;
```

保留 `blockSize` 字段本身，铺师可以显式写 `0.9`、`0.85`、`1.1` 等值表达格内 item 大小变化。

---

## 5. 视觉场景示例

### 5.1 同矩阵、不同 blockSize（SCALE_ONLY）

Formation A: `4×4, blockSize=1.0` → Formation B: `4×4, blockSize=0.85`

| 量 | 变化 |
|----|------|
| 矩阵外框 | 1024×1024 → **不变** |
| 单格逻辑尺寸 | 256×256 → **不变** |
| 格内块 | 256px → 218px（动画插值） |
| 铺师意图 | 段末收紧、快打段留白 |

### 5.1.1 矩阵大小伸缩转换（blockSize-only）

新增/明确一种铺面用法：**矩阵大小伸缩转换**，即只改变 `blockSize`，不改变 `rows` / `cols`。

`.mma` 六字段仍不变：

```text
time,rows,cols,transformType,transformDurationMs,blockSize
```

示例：

```text
12000,4,4,100,300,0.85
16000,4,4,100,300,1.00
```

含义：

- `rows` / `cols` 与上一 formation 相同。
- `transformType = 100` (`SCALE_ONLY`)。
- `transformDurationMs > 0` 时，`blockSize` 从上一值平滑插值到当前值。
- 矩阵逻辑格位置、按键列映射、判定窗口全部不变。
- 这是唯一首版明确允许变换中继续判定的动画类型。

实现上不需要新增 `.mma` 字段；只需要在 parser / builder / renderer 中把 `SCALE_ONLY + rows/cols 不变 + blockSize 变化` 视为 blockSize-only 变换。

### 5.2 不同矩阵、同 blockSize

| Formation | 矩阵像素尺寸 | 单格逻辑 | 格内块 (0.9) |
|-----------|-------------|----------|--------------|
| 3×2 | 768×512 | 256×256 | 230×230 |
| 4×4 | 1024×1024 | 256×256 | 230×230 |
| 6×4 | 1536×1024 | 256×256 | 230×230 |

**同一 `blockSize` 在不同矩阵下视觉比例一致**；6×4 不再出现「格太小」问题。

### 5.3 与贴图分辨率

`res/*.png` 为 256×256，逻辑格也是 256×256；`blockSize=1.0` 时 item 以默认像素尺寸绘制，`blockSize` 改变时才发生缩放。

---

## 6. 判定策略

### 6.1 不变部分

- `JudgeQueue`：`onKeyPress(col)` / `onKeyRelease(col)` 按**列索引 + 歌曲时间**判定。
- `getKeyMapping()`：按键 → `scrollWindow.startCol + keyIndex` 映射列号。
- OD 窗口、`timing_offset_ms`：与像素无关。

### 6.2 需修改：变换期间判定锁

**现状**（`playing_state.cpp`）：

```cpp
if (inTransition || inScroll) {
    // update: 不处理输入、不 auto-miss
}
// handleKeyEvent:
if (inTransition || scrolling) return;  // 直接丢弃按键
```

**目标**：

| 场景 | 判定 |
|------|------|
| `SCALE_ONLY` / blockSize-only（仅 blockSize 变） | **允许**按键判定 + auto-miss |
| `NONE` 瞬间切行列（无动画） | **允许**（行列已切，映射已更新） |
| `SLIDE_*` / `ROTATE_*`（行列结构变） | **维持锁定**至过渡结束（避免列映射与视觉不一致） |
| `scrolling`（列窗口滚动） | **维持锁定**（现有设计） |

实现草图：

```cpp
bool blocksJudgment = m_scrollWindow.scrolling
    || (inTransition && !isScaleOnlyTransition());
```

`isScaleOnlyTransition()`：`transformType == MatrixTransform::SCALE_ONLY`，并且 `prev.rows == next.rows && prev.cols == next.cols`。

首版**不放开** `SLIDE_*` / `ROTATE_*` 的判定，避免行列结构变化时按键映射与视觉位置不一致。

### 6.3 风险与缓解

| 风险 | 缓解 |
|------|------|
| SCALE_ONLY 动画中玩家困惑 | 仅格内 item 缩放，判定列不变；可选 HUD 提示 |
| 大矩阵宽于屏幕 | 滚动逻辑不变；note 在屏外不绘制（现有裁剪） |

---

## 7. 涉及文件与改动清单

### 7.1 新增

| 文件 | 职责 |
|------|------|
| `src/renderer/grid_layout.h` | 常量 + `GridLayout` 布局计算 |
| `src/renderer/grid_layout.cpp` | `cellCenter` / `activeBandX` / `scrollDeltaPx` |

### 7.2 修改

| 文件 | 改动 |
|------|------|
| `note_renderer.cpp` | 用 `GridLayout` 替代本地 `gw/gh`；删除 `effectiveBlockSize` |
| `renderer.cpp` | `setScrollState` 内 `scrollOffset` 用固定 `gw` |
| `playing_state.cpp` | 调试 HUD / ImGui 列指示器坐标；判定锁细分（§6.2） |
| `formation_controller.cpp` | `cellToScreen` 委托 `GridLayout` |
| `border_renderer.cpp` | 布局委托 `GridLayout` |
| `beatmap.h` | 更新 `Formation.blockSize` 注释 |
| `osu_parser.h` | `kDefaultBlockSize` 改为 `1.0` |
| `mma_parser.h` / 规范文档 | 说明 blockSize 新语义（可选补充说明，**字段格式不变**） |

### 7.3 删除 / 回退

- `NoteRenderer::effectiveBlockSize()` 及 `kMaxCellGapPx`（§4.4）
- 各处重复的 `(W - 2*margin) / cols`

### 7.4 不改

- `.mma` 六字段格式
- `JudgeQueue` 判定窗口算法
- 纹理资源尺寸（256×256）

---

## 8. 实现阶段

### Phase 1 — 布局统一（纯视觉）

1. 实现 `GridLayout`
2. 迁移 `note_renderer` + `renderer::scrollOffset`
3. 目视验证：3×2 / 4×4 / 6×4 格宽一致

### Phase 2 — 周边对齐

1. `playing_state` ImGui 列标记、debug HUD
2. `border_renderer`、`formation_controller::cellToScreen`
3. 删除 `effectiveBlockSize`

### Phase 3 — 判定策略

1. 细分 `inTransition` 锁定条件
2. 只放开 `SCALE_ONLY + rows/cols 不变` 过渡中的按键判定与 auto-miss
3. 确认 `update()` 中 auto-miss 与 `handleKeyEvent` 策略一致

### Phase 4 — 打磨（可选）

- `config.ini` 可配格宽
- 编辑器预览文档

---

## 9. 测试计划

| 用例 | 预期 |
|------|------|
| 4×4 `blockSize=1.0` | 单格 256px；活跃 4 列总宽 1024px 居中 |
| 6×4 `blockSize=0.9` | 单格仍 256px；块约 230px；活跃 4 列居中 |
| 3×2 `blockSize=0.9` | 单格 256px；矩阵总宽 768px 居中；缝隙比例与 6×4 一致 |
| `SCALE_ONLY` 0.9→1.0 动画中按键 | Tap/Hold 判定正常，无 Miss 异常 |
| `SCALE_ONLY` 1.0→1.1 动画 | item 可超出逻辑格；矩阵逻辑位置不变 |
| `SLIDE_COL_ADD` 动画中按键 | 仍锁定（与现行为一致） |
| 列滚动中按键 | 仍锁定 |
| 过渡 `blockSize` 插值 | background 与 tap 同步缩放，无错位 |
| osu 导入 formation | 未显式指定时默认 `blockSize=1.0` |

---

## 10. 已确认决策

1. 所有 item 图片资源固定为 **256×256**。
2. 运行时 item 尺寸只基于固定逻辑格和谱面 `blockSize` 变化。
3. 只放开 `SCALE_ONLY + rows/cols 不变` 的变换中判定。
4. 矩阵原则上不会高于屏幕；若出现危险边界，采用裁剪，不整体缩小。
5. `osu_parser` 默认 `blockSize=1.0`。
6. 增加/明确 `SCALE_ONLY` 作为 blockSize-only 的矩阵大小伸缩转换。

---

## 11. 小结

| 维度 | 现方案 | 新方案 |
|------|--------|--------|
| 格宽 | `playW / cols` | **固定 `kDefaultCellW`** |
| 矩阵大小 | 恒填满 play 区域 | **`cols×gw × rows×gh`**，可大于屏幕 |
| blockSize | 格内缩放 + 缝隙补丁 | **纯格内缩放**，语义清晰 |
| SCALE_ONLY | 视觉缩放 | 矩阵外框不变、内容缩放；**判定不中断** |
| 判定 | 列+时间 | **不变** |

确认本文档后按 Phase 1→3 落地实现。
