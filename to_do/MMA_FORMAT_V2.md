# .mma v2 Format Specification

本文档说明 `.mma` 谱面文本格式的 v2 版本，用于创意型音乐游戏中的动态呼吸矩阵系统。v2 的核心目标是让矩阵布局可以随音乐疏密实时变化，并通过清晰的 `transformType` 宏描述缩放、滑入滑出、旋转等过渡方式。

## 1. 文件概述

`.mma` 是 INI 风格的文本谱面格式，用于描述音频信息、谱面元数据、难度参数、动态矩阵阵型变化和音符事件。

v2 相比旧格式的主要变化：
- 新增 `[FormationTransformMacros]`，用于定义矩阵变换类型宏。
- `[Formations]` 的标准行格式调整为 6 字段。
- 删除旧格式中的 `noteTransformType` 字段。
- 明确 `scale`、`slide`、`rotate` 三类矩阵变换的使用规则。

推荐编码：UTF-8。

## 2. 基础语法

- 第一行是格式版本标识，推荐使用 `MMA2`。
- 使用 INI 风格 section，例如 `[General]`。
- `#` 开头的整行表示注释。
- 空行允许存在。
- 键值字段使用 `Key=Value`。
- 列表事件使用逗号分隔字段。
- 所有时间字段统一使用毫秒，类型为 `int64`。

示例：

```ini
MMA2
# source_hash=example

[General]
AudioFilename=audio.mp3
PreviewTime=117887
```

## 3. Section 总览

推荐 section 顺序：

1. `MMA2`
2. 文件注释
3. `[General]`
4. `[Meta]`
5. `[Difficulty]`
6. `[FormationTransformMacros]`
7. `[Formations]`
8. `[Notes]`

各 section 作用：

- `[General]`：通用音频配置。
- `[Meta]`：谱面元信息。
- `[Difficulty]`：难度参数。
- `[FormationTransformMacros]`：矩阵变换类型宏定义。
- `[Formations]`：动态矩阵阵型变化事件。
- `[Notes]`：音符事件。

## 4. `[General]`

字段：

- `AudioFilename string`：音频文件名或相对路径。
- `PreviewTime int64`：预览播放起点，单位毫秒。

示例：

```ini
[General]
AudioFilename=audio.mp3
PreviewTime=117887
```

## 5. `[Meta]`

字段：

- `Title string`：曲名。
- `Artist string`：艺术家。
- `Creator string`：谱师。
- `Version string`：谱面版本或难度名。

示例：

```ini
[Meta]
Title=Hana ni Bourei
Artist=Yorushika
Creator=HowRengar
Version=Blooming
```

## 6. `[Difficulty]`

字段：

- `HP float`：生命或容错参数。
- `OD float`：判定严格度参数。
- `AR float`：预读或显示提前量参数。

示例：

```ini
[Difficulty]
HP=5.5
OD=8.5
AR=9.0
```

## 7. `[FormationTransformMacros]`

该 section 定义 `transformType int32` 的文件级宏。谱面事件中仍写入整数值，解析器、编辑器和文档工具可通过宏表把数字映射为可读名称。

推荐宏定义：

```ini
[FormationTransformMacros]
MATRIX_TRANSFORM_NONE=0
MATRIX_TRANSFORM_SCALE_ONLY=100
MATRIX_TRANSFORM_SLIDE_ROW_ADD_TOP=201
MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTTOM=202
MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTH=203
MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_TOP=204
MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTTOM=205
MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTH=206
MATRIX_TRANSFORM_SLIDE_COL_ADD_LEFT=221
MATRIX_TRANSFORM_SLIDE_COL_ADD_RIGHT=222
MATRIX_TRANSFORM_SLIDE_COL_ADD_BOTH=223
MATRIX_TRANSFORM_SLIDE_COL_REMOVE_LEFT=224
MATRIX_TRANSFORM_SLIDE_COL_REMOVE_RIGHT=225
MATRIX_TRANSFORM_SLIDE_COL_REMOVE_BOTH=226
MATRIX_TRANSFORM_SLIDE_SINGLE_AXIS_COMPLEX=299
MATRIX_TRANSFORM_ROTATE_ROWS_COLS_ADD=301
MATRIX_TRANSFORM_ROTATE_ROWS_ADD_COLS_REMOVE=302
MATRIX_TRANSFORM_ROTATE_ROWS_REMOVE_COLS_ADD=303
MATRIX_TRANSFORM_ROTATE_ROWS_COLS_REMOVE=304
MATRIX_TRANSFORM_ROTATE_COMPLEX=399
```

### 7.1 宏分类

- `scale`：矩阵行列不变，仅整体尺寸或格子缩放变化。
- `slide`：只发生单轴变化，即只增删行或只增删列；列变化可区分左侧、右侧、左右双侧。
- `rotate`：行和列同时变化，使用旋转方式过渡。

### 7.2 宏含义

- `MATRIX_TRANSFORM_NONE=0`：无动画或立即切换。
- `MATRIX_TRANSFORM_SCALE_ONLY=100`：仅缩放，`rows`、`cols` 不变，`blockSize` 改变。
- `MATRIX_TRANSFORM_SLIDE_ROW_ADD_TOP=201`：只新增行，从上侧滑入。
- `MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTTOM=202`：只新增行，从下侧滑入。
- `MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTH=203`：只新增行，从上下双侧滑入。
- `MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_TOP=204`：只删除行，从上侧滑出。
- `MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTTOM=205`：只删除行，从下侧滑出。
- `MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTH=206`：只删除行，从上下双侧滑出。
- `MATRIX_TRANSFORM_SLIDE_COL_ADD_LEFT=221`：只新增列，从左侧滑入。
- `MATRIX_TRANSFORM_SLIDE_COL_ADD_RIGHT=222`：只新增列，从右侧滑入。
- `MATRIX_TRANSFORM_SLIDE_COL_ADD_BOTH=223`：只新增列，从左右双侧滑入，支持两侧各新增一列或多列。
- `MATRIX_TRANSFORM_SLIDE_COL_REMOVE_LEFT=224`：只删除列，从左侧滑出。
- `MATRIX_TRANSFORM_SLIDE_COL_REMOVE_RIGHT=225`：只删除列，从右侧滑出。
- `MATRIX_TRANSFORM_SLIDE_COL_REMOVE_BOTH=226`：只删除列，从左右双侧滑出，支持两侧各删除一列或多列。
- `MATRIX_TRANSFORM_SLIDE_SINGLE_AXIS_COMPLEX=299`：只改变行或只改变列，但侧向分配、节奏编排或动画表现无法归入以上规则。
- `MATRIX_TRANSFORM_ROTATE_ROWS_COLS_ADD=301`：行和列同时增加。
- `MATRIX_TRANSFORM_ROTATE_ROWS_ADD_COLS_REMOVE=302`：行增加且列减少。
- `MATRIX_TRANSFORM_ROTATE_ROWS_REMOVE_COLS_ADD=303`：行减少且列增加。
- `MATRIX_TRANSFORM_ROTATE_ROWS_COLS_REMOVE=304`：行和列同时减少。
- `MATRIX_TRANSFORM_ROTATE_COMPLEX=399`：多行列复合变化、特殊编排变化，或无法归入以上规则的旋转过渡。

## 8. `[Formations]`

`[Formations]` 描述动态呼吸矩阵的阵型变化事件。事件应按时间升序排列。

v2 标准格式：

```text
time,rows,cols,transformType,transformDurationMs,blockSize
```

兼容短格式：

```text
time,rows,cols
```

字段说明：

- `time int64`：阵型变换触发时间点，单位毫秒。例如 `12295` 表示 12.295 秒。
- `rows int32`：目标矩阵行数，范围 `1-4`。
- `cols int32`：目标矩阵列数，范围 `3-6`。
- `transformType int32`：矩阵变换类型，取值来自 `[FormationTransformMacros]`。
- `transformDurationMs int64`：变换动画时长，单位毫秒。例如 `500` 表示 0.5 秒。
- `blockSize float`：格子缩放比例。例如 `0.9` 表示 90%。

短格式默认值：

- `transformType = MATRIX_TRANSFORM_NONE`
- `transformDurationMs = 0`
- `blockSize = 1.0`

已删除字段：

- `noteTransformType int32`：note 变换类型，v2 不再使用。旧格式中该字段可能出现在第 7 位，例如 `...,0`，迁移到 v2 时应删除。

示例：

```ini
[Formations]
0,4,5
12295,4,4,225,500,0.9
14295,4,6,223,500,0.9
22295,4,5,225,500,0.9
```

## 9. `transformType` 判定规则

给定上一个阵型和当前阵型：

```text
rowDelta = rows - prevRows
colDelta = cols - prevCols
```

推荐判定：

- `rowDelta == 0 && colDelta == 0 && blockSize != prevBlockSize`：使用 `MATRIX_TRANSFORM_SCALE_ONLY`。
- `rowDelta > 0 && colDelta == 0`：按设计方向选择 `MATRIX_TRANSFORM_SLIDE_ROW_ADD_TOP`、`MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTTOM` 或 `MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTH`。
- `rowDelta < 0 && colDelta == 0`：按设计方向选择 `MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_TOP`、`MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTTOM` 或 `MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTH`。
- `rowDelta == 0 && colDelta > 0`：按设计方向选择 `MATRIX_TRANSFORM_SLIDE_COL_ADD_LEFT`、`MATRIX_TRANSFORM_SLIDE_COL_ADD_RIGHT` 或 `MATRIX_TRANSFORM_SLIDE_COL_ADD_BOTH`。
- `rowDelta == 0 && colDelta < 0`：按设计方向选择 `MATRIX_TRANSFORM_SLIDE_COL_REMOVE_LEFT`、`MATRIX_TRANSFORM_SLIDE_COL_REMOVE_RIGHT` 或 `MATRIX_TRANSFORM_SLIDE_COL_REMOVE_BOTH`。
- `rowDelta > 0 && colDelta > 0`：使用 `MATRIX_TRANSFORM_ROTATE_ROWS_COLS_ADD`。
- `rowDelta > 0 && colDelta < 0`：使用 `MATRIX_TRANSFORM_ROTATE_ROWS_ADD_COLS_REMOVE`。
- `rowDelta < 0 && colDelta > 0`：使用 `MATRIX_TRANSFORM_ROTATE_ROWS_REMOVE_COLS_ADD`。
- `rowDelta < 0 && colDelta < 0`：使用 `MATRIX_TRANSFORM_ROTATE_ROWS_COLS_REMOVE`。
- 其他特殊情况：使用 `MATRIX_TRANSFORM_ROTATE_COMPLEX`。

原则：

- 只要格子大小不变且只改变一个轴，就优先使用 `slide`。
- 列数从 `4` 变为 `6`，如果设计为左右两侧各滑入一列，则使用 `MATRIX_TRANSFORM_SLIDE_COL_ADD_BOTH`。
- 列数从 `6` 变为 `4`，如果设计为左右两侧各滑出一列，则使用 `MATRIX_TRANSFORM_SLIDE_COL_REMOVE_BOTH`。
- 如果同一时间行数和列数都发生变化，则使用 `rotate`。

## 10. `[Notes]`

`[Notes]` 描述实际音符事件。音符的 `row` 和 `col` 应落在音符时间点对应的矩阵范围内。

Tap note 格式：

```text
time,row,col,T
```

Hold note 格式：

```text
time,row,col,H,endTime
```

字段说明：

- `time int64`：音符开始时间，单位毫秒。
- `row int32`：音符所在行，通常满足 `0 <= row < rows`。
- `col int32`：音符所在列，通常满足 `0 <= col < cols`。
- `type string`：音符类型，`T` 表示 Tap，`H` 表示 Hold。
- `endTime int64`：Hold 结束时间，仅 `H` 类型需要，单位毫秒。

示例：

```ini
[Notes]
1295,2,0,T
22295,0,1,H,22395
```

## 11. 校验规则

解析器或谱面编辑工具应校验：

- 文件头版本合法。
- section 名称合法。
- `[Formations]` 中 `time` 单调非递减，推荐严格递增。
- `rows` 范围为 `1-4`。
- `cols` 范围为 `3-6`。
- `transformType` 必须存在于 `[FormationTransformMacros]` 中。
- `transformDurationMs >= 0`。
- `blockSize > 0`，推荐范围 `0.5-1.5`。
- `[Notes]` 中音符时间应位于音频有效范围内。
- 音符 `row`、`col` 应落在音符时间对应的矩阵范围内。
- Hold note 的 `endTime` 必须大于 `time`。

## 12. 兼容与迁移

从旧格式迁移到 v2：

1. 将文件头从 `MMA1` 改为 `MMA2`。如果现有解析器暂时只接受 `MMA1`，可先保持 `MMA1` 并通过新增 section 表示 v2 能力。
2. 新增 `[FormationTransformMacros]`。
3. 将 `[Formations]` 的 7 字段行从旧格式迁移为 v2 标准格式。

旧格式：

```text
time,rows,cols,transformType,transformDurationMs,blockSize,noteTransformType
```

v2 标准格式：

```text
time,rows,cols,transformType,transformDurationMs,blockSize
```

迁移时应删除末尾的 `noteTransformType`，并按相邻阵型差异与设计侧向重新确认 `transformType`。

## 13. 完整示例

```ini
MMA2
# source_hash=example

[General]
AudioFilename=audio.mp3
PreviewTime=117887

[Meta]
Title=Hana ni Bourei
Artist=Yorushika
Creator=HowRengar
Version=Blooming

[Difficulty]
HP=5.5
OD=8.5
AR=9.0

[FormationTransformMacros]
MATRIX_TRANSFORM_NONE=0
MATRIX_TRANSFORM_SCALE_ONLY=100
MATRIX_TRANSFORM_SLIDE_ROW_ADD_TOP=201
MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTTOM=202
MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTH=203
MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_TOP=204
MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTTOM=205
MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTH=206
MATRIX_TRANSFORM_SLIDE_COL_ADD_LEFT=221
MATRIX_TRANSFORM_SLIDE_COL_ADD_RIGHT=222
MATRIX_TRANSFORM_SLIDE_COL_ADD_BOTH=223
MATRIX_TRANSFORM_SLIDE_COL_REMOVE_LEFT=224
MATRIX_TRANSFORM_SLIDE_COL_REMOVE_RIGHT=225
MATRIX_TRANSFORM_SLIDE_COL_REMOVE_BOTH=226
MATRIX_TRANSFORM_SLIDE_SINGLE_AXIS_COMPLEX=299
MATRIX_TRANSFORM_ROTATE_ROWS_COLS_ADD=301
MATRIX_TRANSFORM_ROTATE_ROWS_ADD_COLS_REMOVE=302
MATRIX_TRANSFORM_ROTATE_ROWS_REMOVE_COLS_ADD=303
MATRIX_TRANSFORM_ROTATE_ROWS_COLS_REMOVE=304
MATRIX_TRANSFORM_ROTATE_COMPLEX=399

[Formations]
0,4,5
12295,4,4,225,500,0.9
14295,4,6,223,500,0.9
22295,4,5,225,500,0.9

[Notes]
1295,2,0,T
22295,0,1,H,22395
```
