# Melody-Matrix 游戏玩法重构 — 实施计划

> 文档版本: v1.0 | 最后更新: 2026-06-16
> 本文档记录所有已确认设计方案、待实施功能模块的技术细节与开发优先级。

---

## 目录

1. [总体架构变更概览](#1-总体架构变更概览)
2. [阶段一：核心判定与输入（P0）](#2-阶段一核心判定与输入p0)
3. [阶段二：渲染管线集成与列滚动（P1）](#3-阶段二渲染管线集成与列滚动p1)
4. [阶段三：Hold保护与高级功能（P2）](#4-阶段三hold保护与高级功能p2)
5. [数据结构设计](#5-数据结构设计)
6. [文件格式变更](#6-文件格式变更)
7. [谱面验证规则](#7-谱面验证规则)
8. [按键映射完整规范](#8-按键映射完整规范)

---

## 1. 总体架构变更概览

### 1.1 当前问题总结

| 问题ID | 描述 | 严重度 |
|--------|------|--------|
| C1 | JudgeQueue 单头指针，多列判定失效 | 致命 |
| C2 | processInput() 无边沿检测，按住每帧触发 | 致命 |
| C3 | 按键-列映射断裂，onKeyPress不传列 | 致命 |
| C4 | Hold音符无释放检测 | 致命 |
| C5 | NoteRenderer/EffectRenderer/HudRenderer未被调用 | 致命 |
| H1 | ScoreManager::addScore()对Miss也增加hitNotes | 高 |
| H2 | 准确度计算不一致（ScoreManager vs ResultState） | 高 |
| H3 | resetGameplay()不重置子系统 | 高 |
| H4 | HP drain rate未从谱面设置 | 高 |
| H5 | JudgeQueue事件回调从未连接 | 高 |
| H6 | InputManager完全未接入 | 高 |
| H7 | 无歌曲前导/倒计时 | 高 |
| M1 | FormationController::update()每帧O(n)遍历 | 中 |
| M2 | NoteRenderer遍历全部音符 | 中 |
| M3 | NotePool/NoteFactory从未使用 | 中 |
| M6 | Renderer硬编码阵型维度 | 中 |

### 1.2 阶段划分

```
阶段一 (P0): 核心判定与输入
  → 游戏从"不可玩"变为"基本可玩"

阶段二 (P1): 渲染管线集成 + 列滚动
  → 游戏从"基本可玩"变为"视觉完整"

阶段三 (P2): Hold保护 + 高级功能
  → 游戏从"视觉完整"变为"体验完善"
```

---

## 2. 阶段一：核心判定与输入（P0）

### 2.1 重构 JudgeQueue 为多列判定

**目标**：每列独立判定，onKeyPress/onKeyRelease 接受列参数。

#### 2.1.1 新数据结构

```cpp
// judge_queue.h

/// 单列判定队列
struct ColumnQueue {
    std::vector<beatmap::Note> notes;  ///< 该列的音符（按时间升序）
    size_t head = 0;                    ///< 当前头部索引

    bool finished() const { return head >= notes.size(); }
    const beatmap::Note& front() const { return notes[head]; }
    void advance() { ++head; }
    void reset() { head = 0; notes.clear(); }
};

/// 多列判定队列 — 每列独立维护
class JudgeQueue {
public:
    static constexpr int MAX_COLS = 6;

    JudgeQueue();

    /// 将音符分配到各列（按 col 字段分发）
    void loadNotes(const std::vector<beatmap::Note>& notes);

    /// 设置判定策略
    void setStrategy(std::unique_ptr<IJudgeStrategy> strategy);

    /// 每帧更新：检查所有列的自动 Miss
    void update(int64_t nowMs, float od);

    /// 在给定列上处理按键按下
    /// @param pressTimeMs  按键时间
    /// @param column       列索引 (0-based)
    /// @param od           难度参数
    JudgmentResult onKeyPress(int64_t pressTimeMs, int32_t column, float od);

    /// 在给定列上处理按键释放（Hold判定用）
    /// @param releaseTimeMs  释放时间
    /// @param column         列索引
    /// @param od             难度参数
    HoldReleaseResult onKeyRelease(int64_t releaseTimeMs, int32_t column, float od);

    /// 获取指定列的活跃Hold音符（正在按住中）
    const beatmap::Note* getActiveHold(int32_t column) const;

    /// 是否所有列都已完成
    bool finished() const;

    /// 重置
    void reset();

    // ── 渲染器访问接口 ──
    /// 获取指定列的队列（供渲染器遍历可见音符）
    const ColumnQueue& columnQueue(int32_t col) const;

    /// 获取总列数
    int32_t columnCount() const { return m_columnCount; }

    // ── 事件回调 ──
    std::function<void(const NoteHitEvent&)>  onHit;
    std::function<void(const NoteMissEvent&)> onMiss;

private:
    void commitHit(int32_t column, JudgmentResult result, int64_t pressTimeMs);
    void emitMiss(int32_t column);

    std::array<ColumnQueue, MAX_COLS> m_columns;
    int32_t m_columnCount = 0;

    /// 每列的活跃Hold状态
    struct ActiveHoldState {
        bool holding = false;     ///< 是否正在按住
        int64_t pressTimeMs = 0;  ///< 按下时间
    };
    std::array<ActiveHoldState, MAX_COLS> m_activeHolds;

    std::unique_ptr<IJudgeStrategy> m_strategy;
};
```

#### 2.1.2 onKeyPress 实现逻辑

```cpp
JudgmentResult JudgeQueue::onKeyPress(int64_t pressTimeMs, int32_t column, float od) {
    if (column < 0 || column >= m_columnCount) return JudgmentResult::Ignored;

    auto& colQ = m_columns[column];
    if (colQ.finished()) return JudgmentResult::Ignored;

    const auto& note = colQ.front();
    const int32_t pw = m_strategy->perfectWindow(od);
    const int32_t gw = m_strategy->goodWindow(od);
    const int64_t dt = pressTimeMs - note.time;

    // 早期按键
    if (dt < -static_cast<int64_t>(gw)) return JudgmentResult::Ignored;

    // Tap 判定
    if (note.type == beatmap::NoteType::Tap) {
        if (std::abs(dt) <= pw) {
            commitHit(column, JudgmentResult::Perfect, pressTimeMs);
            return JudgmentResult::Perfect;
        }
        if (std::abs(dt) <= gw) {
            commitHit(column, JudgmentResult::Good, pressTimeMs);
            return JudgmentResult::Good;
        }
        return JudgmentResult::Ignored;
    }

    // Hold 按下 — 进入按住状态
    if (note.type == beatmap::NoteType::Hold) {
        if (std::abs(dt) <= pw) {
            m_activeHolds[column] = {true, pressTimeMs};
            // Hold 按下时先不 commitHit，等释放时再判定
            // 但需要前进 head 以免重复判定
            colQ.advance();
            return JudgmentResult::Perfect; // 按下时机判定
        }
        if (std::abs(dt) <= gw) {
            m_activeHolds[column] = {true, pressTimeMs};
            colQ.advance();
            return JudgmentResult::Good;
        }
        return JudgmentResult::Ignored;
    }

    return JudgmentResult::Ignored;
}
```

#### 2.1.3 onKeyRelease 实现逻辑

```cpp
/// Hold释放判定结果
enum class HoldReleaseResult {
    Ignored = 0,     ///< 该列没有活跃Hold
    Perfect = 1,     ///< 释放时机在Perfect窗口内
    Good    = 2,     ///< 释放时机在Good窗口内
    Miss    = 3,     ///< 过早释放
};

HoldReleaseResult JudgeQueue::onKeyRelease(int64_t releaseTimeMs, int32_t column, float od) {
    if (column < 0 || column >= m_columnCount) return HoldReleaseResult::Ignored;

    auto& hold = m_activeHolds[column];
    if (!hold.holding) return HoldReleaseResult::Ignored;

    // 找到对应的Hold音符（已被advance，需要从已判定的音符中获取）
    // 方案：在ActiveHoldState中存储holdEnd
    const int64_t holdEnd = hold.holdEndTimeMs;
    const int32_t pw = m_strategy->perfectWindow(od);
    const int32_t gw = m_strategy->goodWindow(od);
    const int64_t dt = releaseTimeMs - holdEnd;

    hold.holding = false;

    if (std::abs(dt) <= pw) {
        return HoldReleaseResult::Perfect;
    }
    if (std::abs(dt) <= gw) {
        return HoldReleaseResult::Good;
    }
    if (dt < -static_cast<int64_t>(gw)) {
        // 过早释放 — Hold Miss
        return HoldReleaseResult::Miss;
    }
    // 过晚释放 — 仍算Good（宽松处理）
    return HoldReleaseResult::Good;
}
```

#### 2.1.4 update() 自动Miss逻辑

```cpp
void JudgeQueue::update(int64_t nowMs, float od) {
    const int64_t miss = m_strategy->missThreshold(od);

    for (int32_t col = 0; col < m_columnCount; ++col) {
        auto& colQ = m_columns[col];

        // 如果该列有活跃Hold，检查是否超时
        if (m_activeHolds[col].holding) {
            // Hold超时自动释放由 onKeyRelease 处理
            // 这里只检查是否已经过了holdEnd很久仍未释放
            // （安全网：如果玩家忘记释放）
            // 暂不处理，留到阶段三
        }

        // 自动Miss：检查每列头部音符
        while (!colQ.finished() && nowMs >= colQ.front().time + miss) {
            // 跳过正在按住的Hold（不应自动Miss）
            if (colQ.front().type == beatmap::NoteType::Hold
                && m_activeHolds[col].holding) {
                break;
            }
            emitMiss(col);
        }
    }
}
```

#### 2.1.5 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/gameplay/judge_queue.h` | 重写 | 多列数据结构，新接口 |
| `src/gameplay/judge_queue.cpp` | 重写 | 多列判定逻辑 |
| `src/core/states/playing_state.h` | 修改 | 适配新接口 |
| `src/core/states/playing_state.cpp` | 修改 | processInput传列参数 |

---

### 2.2 实现输入边沿检测

**目标**：按下边沿触发 onKeyPress，释放边沿触发 onKeyRelease。

#### 2.2.1 PlayingState 新增成员

```cpp
// playing_state.h 新增

/// 按键映射配置
struct KeyMapping {
    int32_t sdlKey;      ///< SDL 按键代码
    int32_t column;      ///< 对应的列索引 (-1 = 未映射)
};

static constexpr int MAX_KEYS = 4;  ///< 全程仅使用 D F J K

/// 获取当前阵型对应的按键映射
std::array<KeyMapping, MAX_KEYS> getCurrentKeyMapping() const;

/// 上一帧的按键状态（边沿检测用）
std::array<bool, MAX_KEYS> m_prevKeyState = {};

/// 当前帧的按键状态
std::array<bool, MAX_KEYS> m_currKeyState = {};
```

#### 2.2.2 按键映射实现

```cpp
std::array<KeyMapping, 4> PlayingState::getCurrentKeyMapping() const {
    int32_t cols = m_formationCtrl.current().cols;

    // 基础4键映射
    // cols=1: F
    // cols=2: F J
    // cols=3: D F J
    // cols=4: D F J K
    // cols=5-6: 列滚动（阶段二）
    std::array<KeyMapping, 4> mapping = {};

    // 默认映射表（居中原则）
    // 列索引 0,1,2,3 对应的键
    static const int32_t keyForIndex[4] = { SDLK_d, SDLK_f, SDLK_j, SDLK_k };

    int32_t activeKeys = std::min(cols, MAX_KEYS);

    // 居中偏移：当cols<4时，从中间取键
    // cols=3: 取 D(0) F(1) J(2) → 映射 col0→D, col1→F, col2→J
    // cols=2: 取 F(1) J(2) → 映射 col0→F, col1→J
    // cols=1: 取 F(1) → 映射 col0→F
    int32_t keyOffset = (MAX_KEYS - activeKeys) / 2;

    for (int32_t i = 0; i < MAX_KEYS; ++i) {
        if (i < activeKeys) {
            mapping[i] = { keyForIndex[keyOffset + i], i };
        } else {
            mapping[i] = { 0, -1 }; // 未使用
        }
    }

    return mapping;
}
```

#### 2.2.3 processInput 重写

```cpp
void PlayingState::processInput() {
    auto& kernel = Kernel::instance();
    int64_t nowMs = kernel.clock().nowMs();
    float od = m_beatmap.difficulty.od;

    auto mapping = getCurrentKeyMapping();
    const Uint8* state = SDL_GetKeyboardState(nullptr);

    for (int32_t i = 0; i < MAX_KEYS; ++i) {
        if (mapping[i].column < 0) continue;

        SDL_Scancode scancode = SDL_GetScancodeFromKey(mapping[i].sdlKey);
        m_currKeyState[i] = state[scancode];

        // 按下边沿（上一帧未按，当前帧按下）
        if (m_currKeyState[i] && !m_prevKeyState[i]) {
            auto result = m_judgeQueue.onKeyPress(nowMs, mapping[i].column, od);

            switch (result) {
            case gameplay::JudgmentResult::Perfect:
                m_perfectCount++;
                m_hitNotes++;
                m_comboManager.onHit();
                m_scoreManager.addScore(gameplay::JudgmentResult::Perfect, m_comboManager.current());
                m_hpManager.onJudgment(gameplay::JudgmentResult::Perfect);
                break;
            case gameplay::JudgmentResult::Good:
                m_goodCount++;
                m_hitNotes++;
                m_comboManager.onHit();
                m_scoreManager.addScore(gameplay::JudgmentResult::Good, m_comboManager.current());
                m_hpManager.onJudgment(gameplay::JudgmentResult::Good);
                break;
            case gameplay::JudgmentResult::Miss:
                m_missCount++;
                m_comboManager.onMiss();
                m_hpManager.onJudgment(gameplay::JudgmentResult::Miss);
                break;
            case gameplay::JudgmentResult::Ignored:
                break;
            }
        }

        // 释放边沿（上一帧按下，当前帧未按）
        if (!m_currKeyState[i] && m_prevKeyState[i]) {
            auto holdResult = m_judgeQueue.onKeyRelease(nowMs, mapping[i].column, od);

            switch (holdResult) {
            case gameplay::HoldReleaseResult::Perfect:
                // Hold完美释放 — 已在onKeyPress时计分，这里可选额外奖励
                break;
            case gameplay::HoldReleaseResult::Good:
                break;
            case gameplay::HoldReleaseResult::Miss:
                // 过早释放 — Hold Miss
                m_missCount++;
                m_comboManager.onMiss();
                m_hpManager.onJudgment(gameplay::JudgmentResult::Miss);
                break;
            case gameplay::HoldReleaseResult::Ignored:
                break;
            }
        }
    }

    // 保存当前帧状态
    m_prevKeyState = m_currKeyState;
}
```

#### 2.2.4 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/core/states/playing_state.h` | 修改 | 新增KeyMapping、边沿检测成员 |
| `src/core/states/playing_state.cpp` | 重写 | processInput完整重写 |

---

### 2.3 修复 ScoreManager

**目标**：Miss 不计 hitNotes，统一准确度公式。

#### 2.3.1 addScore 修复

```cpp
void ScoreManager::addScore(JudgmentResult result, int32_t combo) {
    if (result == JudgmentResult::Ignored) return;
    if (result == JudgmentResult::Miss) {
        // Miss 不加分，不计 hitNotes
        return;
    }
    m_totalScore += scoreForHit(result, combo);
    ++m_hitNotes;
}
```

#### 2.3.2 统一准确度公式

```cpp
// 采用加权准确度（与 ResultState 一致）
float ScoreManager::accuracy() const {
    if (m_totalNotes == 0) return 1.0f;
    // weighted: Perfect=100, Good=60, Miss=0
    float weighted = static_cast<float>(m_perfectCount * 100 + m_goodCount * 60);
    float maxWeighted = static_cast<float>(m_totalNotes * 100);
    return weighted / maxWeighted;
}
```

需要在 ScoreManager 中新增 `m_perfectCount` / `m_goodCount` 字段，或在 PlayingState 中计算准确度时直接使用自己的计数器。

**推荐方案**：ScoreManager 新增 perfectCount/goodCount，由 PlayingState 在判定时同步更新。

#### 2.3.3 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/gameplay/score_manager.h` | 修改 | 新增perfectCount/goodCount |
| `src/gameplay/score_manager.cpp` | 修改 | 修复addScore，统一accuracy |

---

### 2.4 修复 resetGameplay

```cpp
void PlayingState::resetGameplay() {
    m_audio.stop();
    m_audio.shutdown();

    // 重置所有子系统
    m_judgeQueue.reset();
    m_scoreManager.reset();
    m_comboManager.reset();
    m_hpManager.reset();
    m_formationCtrl.reset();

    // 重置游戏状态
    m_gameplayInitialized = false;
    m_songFinished = false;
    m_playerDied = false;
    m_totalNotes = 0;
    m_hitNotes = 0;
    m_perfectCount = 0;
    m_goodCount = 0;
    m_missCount = 0;
    m_needsReinit = false;

    // 重置输入状态
    m_prevKeyState.fill(false);
    m_currKeyState.fill(false);
}
```

#### 2.4.1 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/core/states/playing_state.cpp` | 修改 | resetGameplay补充子系统重置 |

---

### 2.5 设置 HP drain rate

```cpp
void PlayingState::initGameplay() {
    // ... 现有代码 ...

    // ── Init HP drain rate from beatmap ──
    m_hpManager.setDrainRate(m_beatmap.difficulty.hp);

    // ... 现有代码 ...
}
```

#### 2.5.1 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/core/states/playing_state.cpp` | 修改 | initGameplay中设置drain rate |

---

### 2.6 连接 JudgeQueue 事件回调

```cpp
void PlayingState::initGameplay() {
    // ... 现有代码 ...

    // ── Connect judge events ──
    m_judgeQueue.onHit = [this](const gameplay::NoteHitEvent& evt) {
        // 触发特效（阶段二接入EffectRenderer时使用）
        // m_effectRenderer.spawnHitEffect(evt.result, screenX, screenY);
    };
    m_judgeQueue.onMiss = [this](const gameplay::NoteMissEvent& evt) {
        // 触发Miss特效
    };

    // ... 现有代码 ...
}
```

#### 2.6.1 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/core/states/playing_state.cpp` | 修改 | initGameplay中设置回调 |

---

### 2.7 谱面验证：禁止同列 Hold+Tap 重叠

#### 2.7.1 BeatmapBuilder 新增验证规则

```cpp
// beatmap_builder.cpp 新增验证

Result<Beatmap> BeatmapBuilder::build() {
    // ... 现有验证 ...

    // 规则7: 同列 Hold+Tap 不允许重叠
    for (size_t i = 0; i < m_notes.size(); ++i) {
        if (m_notes[i].type != NoteType::Hold) continue;

        for (size_t j = 0; j < m_notes.size(); ++j) {
            if (i == j) continue;
            if (m_notes[j].col != m_notes[i].col) continue;
            if (m_notes[j].type != NoteType::Tap) continue;

            // Tap 时间在 Hold 持续期间内
            if (m_notes[j].time > m_notes[i].time &&
                m_notes[j].time < m_notes[i].holdEnd) {
                return Result<Beatmap>::fail(ErrorCode::BeatmapValidation,
                    "Column " + std::to_string(m_notes[i].col) +
                    ": Tap at t=" + std::to_string(m_notes[j].time) +
                    " overlaps with Hold t=" + std::to_string(m_notes[i].time) +
                    "-" + std::to_string(m_notes[i].holdEnd));
            }
        }
    }

    // ... 继续构建 ...
}
```

#### 2.7.2 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/beatmap/beatmap_builder.cpp` | 修改 | 新增Hold+Tap重叠验证 |

---

### 2.8 歌曲前导倒计时

```cpp
void PlayingState::initGameplay() {
    // ... 现有代码 ...

    // ── Lead-in: 3秒倒计时 ──
    static constexpr int64_t LEAD_IN_MS = 3000;
    m_audio.playSong(m_beatmap.meta.audioFile);
    m_audio.pause(); // 先暂停，等倒计时结束再恢复
    m_leadInRemaining = LEAD_IN_MS;
    m_inLeadIn = true;
}

GameState PlayingState::update(float dt) {
    // ... 现有代码 ...

    // ── Lead-in countdown ──
    if (m_inLeadIn) {
        m_leadInRemaining -= static_cast<int64_t>(dt * 1000);
        if (m_leadInRemaining <= 0) {
            m_inLeadIn = false;
            m_audio.resume();
        }
        // 倒计时期间不处理输入和判定
        return GameState::Count;
    }

    // ... 正常更新逻辑 ...
}
```

#### 2.8.1 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/core/states/playing_state.h` | 修改 | 新增m_inLeadIn, m_leadInRemaining |
| `src/core/states/playing_state.cpp` | 修改 | update中处理倒计时 |

---

### 2.9 阶段一文件变更汇总

| 文件 | 变更类型 | 优先级 |
|------|----------|--------|
| `src/gameplay/judge_queue.h` | 重写 | P0 |
| `src/gameplay/judge_queue.cpp` | 重写 | P0 |
| `src/core/states/playing_state.h` | 修改 | P0 |
| `src/core/states/playing_state.cpp` | 重写processInput | P0 |
| `src/gameplay/score_manager.h` | 修改 | P0 |
| `src/gameplay/score_manager.cpp` | 修改 | P0 |
| `src/beatmap/beatmap_builder.cpp` | 修改 | P0 |

---

## 3. 阶段二：渲染管线集成与列滚动（P1）

### 3.1 基础布局与间距系统

#### 3.1.1 Formation 数据结构扩展

```cpp
// beatmap.h — Formation 扩展

struct Formation {
    int64_t time = 0;       ///< 生效时间（毫秒）
    int32_t rows = 1;       ///< 网格行数
    int32_t cols = 1;       ///< 网格列数

    // ── 间距与尺寸 ──
    float cellWidth  = 0.0f;   ///< 单元格宽度（逻辑像素，0=自动计算）
    float cellHeight = 0.0f;   ///< 单元格高度（逻辑像素，0=自动计算）
    float gapX = 4.0f;         ///< 列间距（逻辑像素）
    float gapY = 4.0f;         ///< 行间距（逻辑像素）

    // ── 变换信息 ──
    enum class TransitionType : uint8_t {
        Fade = 0,       ///< 交叉淡入淡出（默认）
        Insert = 1,     ///< 渐入线段插入（单行/列增减）
        Rotate = 2,     ///< 矩阵旋转（多行/列增减或纯装饰）
    };

    TransitionType transitionType = TransitionType::Fade;
    float transitionDurationMs = 300.0f;  ///< 过渡持续时间
    float rotationAngle = 0.0f;           ///< 旋转角度（仅Rotate类型）
    float rotationDirection = 1.0f;       ///< 旋转方向：1=顺时针, -1=逆时针
};
```

#### 3.1.2 cellToScreen 更新

```cpp
// formation_controller.cpp — 支持间距和自定义尺寸

void FormationController::cellToScreen(
    int row, int col,
    const beatmap::Formation& formation,
    float& outX, float& outY)
{
    const float W = 1920.0f, H = 1080.0f;

    float cellW = formation.cellWidth;
    float cellH = formation.cellHeight;
    float gapX = formation.gapX;
    float gapY = formation.gapY;

    // 自动计算尺寸（如果未指定）
    if (cellW <= 0.0f) {
        float totalGapX = gapX * (formation.cols - 1);
        cellW = (W - 2 * 120.0f - totalGapX) / formation.cols;
    }
    if (cellH <= 0.0f) {
        float totalGapY = gapY * (formation.rows - 1);
        cellH = (H - 2 * 120.0f - totalGapY) / formation.rows;
    }

    // 居中偏移
    float totalW = formation.cols * cellW + (formation.cols - 1) * gapX;
    float totalH = formation.rows * cellH + (formation.rows - 1) * gapY;
    float offsetX = (W - totalW) / 2.0f;
    float offsetY = (H - totalH) / 2.0f;

    outX = offsetX + col * (cellW + gapX) + cellW * 0.5f;
    outY = offsetY + row * (cellH + gapY) + cellH * 0.5f;
}
```

#### 3.1.3 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/beatmap/beatmap.h` | 修改 | Formation扩展字段 |
| `src/gameplay/formation_controller.h/.cpp` | 修改 | cellToScreen支持间距 |
| `src/renderer/renderer.h/.cpp` | 修改 | 渲染网格使用新坐标 |
| `src/renderer/note_renderer.cpp` | 修改 | 音符定位使用新坐标 |

---

### 3.2 视觉过渡系统

#### 3.2.1 过渡类型枚举与数据

```cpp
// formation_controller.h 新增

/// 阵型过渡动画状态
struct FormationTransition {
    beatmap::Formation from;           ///< 旧阵型
    beatmap::Formation to;             ///< 新阵型
    int64_t startTimeMs = 0;           ///< 过渡开始时间
    float durationMs = 300.0f;         ///< 过渡持续时间
    beatmap::Formation::TransitionType type = beatmap::Formation::TransitionType::Fade;

    // 旋转参数（仅Rotate类型）
    float rotationAngle = 0.0f;
    float rotationDirection = 1.0f;

    float progress(int64_t nowMs) const {
        if (durationMs <= 0) return 1.0f;
        float t = static_cast<float>(nowMs - startTimeMs) / durationMs;
        return std::max(0.0f, std::min(1.0f, t));
    }

    bool finished(int64_t nowMs) const {
        return nowMs >= startTimeMs + static_cast<int64_t>(durationMs);
    }
};
```

#### 3.2.2 渲染器过渡实现

```cpp
// renderer.cpp — 过渡渲染逻辑

void Renderer::renderGrid(int64_t timeMs) {
    if (m_transition) {
        float p = m_transition->progress(timeMs);

        switch (m_transition->type) {
        case Formation::TransitionType::Fade:
            renderGridFade(m_transition->from, m_transition->to, p);
            break;

        case Formation::TransitionType::Insert:
            renderGridInsert(m_transition->from, m_transition->to, p);
            break;

        case Formation::TransitionType::Rotate:
            renderGridRotate(m_transition->from, m_transition->to, p,
                           m_transition->rotationAngle,
                           m_transition->rotationDirection);
            break;
        }

        if (m_transition->finished(timeMs)) {
            m_transition.reset();
        }
    } else {
        renderGridStatic(m_currentFormation);
    }
}
```

#### 3.2.3 Fade 过渡实现

```cpp
void Renderer::renderGridFade(const Formation& from, const Formation& to, float progress) {
    float easedP = easeInOutCubic(progress);

    // 旧网格淡出
    float oldAlpha = 0.3f * (1.0f - easedP);
    renderGridLines(from, oldAlpha);

    // 新网格淡入
    float newAlpha = 0.3f * easedP;
    renderGridLines(to, newAlpha);

    // 音符位置插值
    // 每个可见音符：pos = lerp(oldPos, newPos, easedP)
}
```

#### 3.2.4 Insert 过渡实现

```cpp
void Renderer::renderGridInsert(const Formation& from, const Formation& to, float progress) {
    float easedP = easeInOutCubic(progress);

    // 场景：从 from.rows=2, from.cols=4 到 to.rows=3, to.cols=4
    // 上方新增一行

    // 1. 现有行：格子高度从 from 尺寸平滑缩小到 to 尺寸
    float currentCellH = lerp(fromCellH, toCellH, easedP);
    float currentGapY = lerp(fromGapY, toGapY, easedP);

    // 2. 绘制现有行的网格线（位置随缩小而移动）
    renderGridLinesWithSize(to, currentCellH, to.cellWidth, currentGapY, to.gapX, 0.3f);

    // 3. 新行：线段渐入（alpha: 0→0.3）
    float newRowAlpha = 0.3f * easedP;
    renderNewRowLines(to, to.rows - 1, currentCellH, currentGapY, newRowAlpha);
}
```

#### 3.2.5 Rotate 过渡实现

```cpp
void Renderer::renderGridRotate(const Formation& from, const Formation& to, float progress,
                                 float angle, float direction) {
    // 旋转动画：
    // progress 0.0 → 0.5: 旧矩阵旋转，角度从0到angle/2
    // progress 0.5: 瞬间切换到新矩阵大小
    // progress 0.5 → 1.0: 新矩阵继续旋转，角度从angle/2到angle

    float halfAngle = angle * direction * 0.5f;

    if (progress <= 0.5f) {
        // 前半段：旧矩阵旋转
        float p = progress / 0.5f; // 0→1
        float currentAngle = halfAngle * easeInOutCubic(p);

        glPushMatrix(); // 或用 shader uniform
        applyRotation(currentAngle);
        renderGridLines(from, 0.3f);
        glPopMatrix();
    } else {
        // 后半段：新矩阵旋转
        float p = (progress - 0.5f) / 0.5f; // 0→1
        float currentAngle = halfAngle * (1.0f - easeInOutCubic(p));

        glPushMatrix();
        applyRotation(currentAngle);
        renderGridLines(to, 0.3f);
        glPopMatrix();
    }
}
```

#### 3.2.6 变换期间按键时间忽略

```cpp
// playing_state.cpp — 变换期间禁止判定

GameState PlayingState::update(float dt) {
    // ... 现有代码 ...

    // ── 检查阵型过渡状态 ──
    bool inTransition = m_formationCtrl.inTransition(nowMs);

    // 过渡期间不处理输入
    if (!inTransition && !m_inLeadIn) {
        processInput();
    }

    // 过渡期间JudgeQueue仍需update（自动Miss检查）
    // 但由于500ms缓冲规则，过渡期间不应有音符需要判定
    m_judgeQueue.update(nowMs, od);

    // ... 现有代码 ...
}
```

#### 3.2.7 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/gameplay/formation_controller.h/.cpp` | 修改 | 过渡状态管理 |
| `src/renderer/renderer.h/.cpp` | 重写 | 过渡渲染逻辑 |
| `src/core/states/playing_state.cpp` | 修改 | 过渡期间输入屏蔽 |

---

### 3.3 集成 NoteRenderer / EffectRenderer / HudRenderer

#### 3.3.1 Renderer 持有子渲染器

```cpp
// renderer.h 修改

class Renderer {
public:
    // ... 现有接口 ...

    /// 获取特效渲染器（供 PlayingState 触发特效）
    EffectRenderer& effectRenderer() { return m_effectRenderer; }

    /// 更新当前阵型（由 PlayingState 调用）
    void updateFormation(const beatmap::Formation& current);

    /// 更新可见音符数据（由 PlayingState 调用）
    void updateNotes(const std::vector<beatmap::Note>& notes,
                     const gameplay::JudgeQueue& judgeQueue,
                     int64_t timeMs, float ar);

private:
    // ... 现有成员 ...

    NoteRenderer m_noteRenderer;
    EffectRenderer m_effectRenderer;
    HudRenderer m_hudRenderer;

    beatmap::Formation m_currentFormation;
    std::vector<beatmap::Note> m_visibleNotes;
};
```

#### 3.3.2 renderFrame 更新

```cpp
void Renderer::renderFrame(int64_t interpolatedTimeMs) {
    if (!m_initialized) return;

    renderBackground();

    if (m_gameplayRendering) {
        renderGrid(interpolatedTimeMs);
        m_noteRenderer.render(m_visibleNotes, interpolatedTimeMs,
                             m_currentFormation.rows, m_currentFormation.cols,
                             m_currentAR);
        m_effectRenderer.render();
        // HUD 由 ImGui overlay 绘制（阶段二暂时保留）
    }
}

void Renderer::update(float dt) {
    m_effectRenderer.update(dt);
}
```

#### 3.3.3 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/renderer/renderer.h/.cpp` | 修改 | 集成子渲染器 |
| `src/core/states/playing_state.cpp` | 修改 | 调用Renderer更新接口 |

---

### 3.4 列滚动机制（cols > 4）

#### 3.4.1 滚动窗口管理

```cpp
// playing_state.h 新增

/// 列滚动窗口状态
struct ScrollWindow {
    int32_t startCol = 0;       ///< 窗口起始列
    int32_t endCol = 3;         ///< 窗口结束列（含）
    bool scrolling = false;     ///< 是否正在滚动
    int64_t scrollStartMs = 0;  ///< 滚动开始时间
    float scrollDurationMs = 200.0f; ///< 滚动动画时长
    int32_t targetStartCol = 0; ///< 目标起始列
};
```

#### 3.4.2 滚动期间禁止判定

```
规则：
  1. 滚动动画开始前，确保滚动期间没有音符需要判定
  2. 滚动期间（200ms），屏蔽所有按键输入
  3. 滚动完成后恢复输入

实现：
  - 在 processInput() 中检查 m_scrollWindow.scrolling
  - 如果正在滚动，跳过所有按键处理
  - 滚动完成由 update() 检测并更新映射
```

#### 3.4.3 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/core/states/playing_state.h` | 修改 | 新增ScrollWindow |
| `src/core/states/playing_state.cpp` | 修改 | 滚动逻辑 |
| `src/renderer/note_renderer.cpp` | 修改 | 非活跃列低透明度 |

---

### 3.5 阶段二文件变更汇总

| 文件 | 变更类型 | 优先级 |
|------|----------|--------|
| `src/beatmap/beatmap.h` | 修改 | P1 |
| `src/gameplay/formation_controller.h/.cpp` | 修改 | P1 |
| `src/renderer/renderer.h/.cpp` | 重写 | P1 |
| `src/renderer/note_renderer.cpp` | 修改 | P1 |
| `src/core/states/playing_state.h/.cpp` | 修改 | P1 |

---

## 4. 阶段三：Hold保护与高级功能（P2）

### 4.1 Hold保护机制

#### 4.1.1 ActiveHoldState 扩展

```cpp
// judge_queue.h 扩展

struct ActiveHoldState {
    bool holding = false;
    int64_t pressTimeMs = 0;
    int64_t holdEndTimeMs = 0;    ///< Hold结束时间
    int32_t originalCol = -1;     ///< Hold创建时的列
    int32_t currentCol = -1;      ///< 当前映射到的列（可能因阵型变化而改变）
    float screenX = 0.0f;         ///< 当前屏幕X（插值用）
    float screenY = 0.0f;         ///< 当前屏幕Y（插值用）
    float targetScreenX = 0.0f;   ///< 目标屏幕X
    float targetScreenY = 0.0f;   ///< 目标屏幕Y
};
```

#### 4.1.2 阵型变化时Hold重映射

```cpp
void JudgeQueue::onFormationChanged(const FormationChangedEvent& evt) {
    for (int32_t i = 0; i < MAX_COLS; ++i) {
        auto& hold = m_activeHolds[i];
        if (!hold.holding) continue;

        // 检查Hold的列在新阵型中是否仍存在
        if (hold.currentCol >= evt.current.cols) {
            // 列被删除 — 寻找最近的可用列
            int32_t newCol = findNearestAvailableCol(hold.currentCol, evt.current.cols);
            if (newCol < 0) {
                // 无可用列 — 强制Good释放
                forceReleaseHold(i, JudgmentResult::Good);
            } else {
                hold.currentCol = newCol;
                // 更新目标屏幕坐标
                FormationController::cellToScreen(
                    /* row */, newCol, evt.current, hold.targetScreenX, hold.targetScreenY);
            }
        }
    }
}
```

#### 4.1.3 按键映射锁定

```
规则：活跃Hold对应的物理键到列的映射在Hold期间锁定

实现：
  1. 阵型变化时，计算新的按键映射
  2. 对每个活跃Hold，确保其物理键仍映射到Hold的currentCol
  3. 其他键的映射围绕锁定键调整
```

#### 4.1.4 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/gameplay/judge_queue.h/.cpp` | 修改 | ActiveHoldState扩展 |
| `src/core/states/playing_state.cpp` | 修改 | Hold保护逻辑 |
| `src/gameplay/formation_controller.cpp` | 修改 | 阵型变化通知 |

---

### 4.2 FormationController 二分查找优化

```cpp
bool FormationController::update(int64_t nowMs) {
    if (m_formations.empty()) return false;

    // 二分查找当前时间对应的阵型
    auto it = std::upper_bound(m_formations.begin(), m_formations.end(), nowMs,
        [](int64_t t, const beatmap::Formation& f) { return t < f.time; });

    size_t newIndex = (it == m_formations.begin()) ? 0 : std::distance(m_formations.begin(), it) - 1;

    if (newIndex != m_currentIndex) {
        // 阵型变化...
        m_currentIndex = newIndex;
        return true;
    }
    return false;
}
```

#### 4.2.1 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|--------|
| `src/gameplay/formation_controller.cpp` | 修改 | 二分查找 |

---

### 4.3 阶段三文件变更汇总

| 文件 | 变更类型 | 优先级 |
|------|----------|--------|
| `src/gameplay/judge_queue.h/.cpp` | 修改 | P2 |
| `src/core/states/playing_state.cpp` | 修改 | P2 |
| `src/gameplay/formation_controller.cpp` | 修改 | P2 |

---

## 5. 数据结构设计

### 5.1 核心数据流

```
SDL_Event
  │
  ▼
Kernel::pumpInputEvents()
  │
  ▼
PlayingState::processInput()
  │ 边沿检测 + 列映射
  ▼
JudgeQueue::onKeyPress(time, column, od)  ──→  onHit/onMiss 回调
  │                                              │
  │                                              ▼
  │                                    EffectRenderer::spawnHitEffect()
  ▼
ScoreManager / ComboManager / HPManager
  │
  ▼
PlayingState::update() ──→ Renderer::updateNotes() ──→ NoteRenderer::render()
```

### 5.2 阵型数据流

```
Beatmap::formations
  │
  ▼
FormationController::load()
  │
  ▼
FormationController::update(nowMs)
  │ 二分查找当前阵型
  │ 检测变化 → FormationChangedEvent
  ▼
Renderer::updateFormation(current)
  │
  ▼
Renderer::renderGrid(timeMs)
  │ 根据过渡类型选择渲染方式
  ▼
NoteRenderer::render(notes, timeMs, formation, ar)
  │ cellToScreen 使用间距系统
  ▼
屏幕输出
```

---

## 6. 文件格式变更

### 6.1 .mma 格式扩展

#### 6.1.1 当前格式

```
MMA1
[General]
AudioFilename=path.mp3
PreviewTime=10000
[Difficulty]
HP=5.0
OD=5.0
AR=5.0
[Meta]
Title=Song Title
Artist=Artist Name
[Formations]
0,3,4
5000,4,5
[Notes]
1000,0,0,T
2000,1,2,H,2500
```

#### 6.1.2 扩展后格式

```
MMA1
[General]
AudioFilename=path.mp3
PreviewTime=10000
[Difficulty]
HP=5.0
OD=5.0
AR=5.0
[Meta]
Title=Song Title
Artist=Artist Name
[Formations]
# 格式: time,rows,cols,cellW,cellH,gapX,gapY,transitionType,transitionDuration,rotationAngle,rotationDirection
# transitionType: 0=Fade, 1=Insert, 2=Rotate
# cellW/cellH: 0=自动计算
# rotationAngle/rotationDirection: 仅Rotate类型有效
0,3,4,0,0,4,4,0,300,0,1
5000,4,5,0,0,4,4,2,400,180,1
10000,3,4,0,0,4,4,1,300,0,1
[Notes]
1000,0,0,T
2000,1,2,H,2500
```

#### 6.1.3 向下兼容

```
解析规则：
  - 旧格式（3字段: time,rows,cols）→ 其余字段使用默认值
  - 新格式（11字段）→ 完整解析
  - 字段数在4-10之间 → 按位置解析，缺失字段用默认值
```

### 6.2 .osz 转换适配

#### 6.2.1 密度分析算法

```
1. 将谱面按2秒窗口分段
2. 计算每段音符密度（notes/second）
3. 移动平均平滑（窗口=4段）
4. 检测密度变化点（梯度 > 阈值）
5. 在变化点放置 Formation

密度→阵型映射：
  低密度（<25百分位）→ 2-3列
  中密度（25-75百分位）→ 3-4列
  高密度（>75百分位）→ 4-5列

过渡类型选择：
  列数变化 ≤ 1 → Insert
  列数变化 > 1 → Rotate
  行数不变且列数不变 → 不生成Formation变化
```

#### 6.2.2 涉及文件

| 文件 | 变更类型 | 说明 |
|------|----------|--------|
| `src/beatmap/mma_parser.h/.cpp` | 修改 | 解析扩展字段 |
| `src/beatmap/mma_serializer.h/.cpp` | 修改 | 序列化扩展字段 |
| `src/beatmap/osu_parser.h/.cpp` | 修改 | 密度分析生成Formation |

---

## 7. 谱面验证规则

### 7.1 完整验证规则清单

| 规则ID | 描述 | 阶段 |
|--------|------|------|
| V1 | 版本号必须为 MMA1 | 已有 |
| V2 | AudioFilename 必须存在 | 已有 |
| V3 | 音符 row/col 必须在阵型范围内 | 已有 |
| V4 | 阵型必须按时间升序 | 已有 |
| V5 | 音符必须按时间升序 | 已有 |
| V6 | 必须有至少1个 Formation | 已有 |
| **V7** | **同列 Hold+Tap 不允许重叠** | **阶段一** |
| **V8** | **阵型变化前500ms内不允许出现新音符** | **阶段一** |
| **V9** | **阵型变化时活跃Hold在新阵型中必须有合法位置** | **阶段三** |

### 7.2 V8 详细规则

```
对于每个 Formation 变化点 F（时间 T）：
  在时间范围 [T - 500ms, T) 内：
    不允许存在任何音符（Tap 或 Hold 的起始时间）

  注意：
  - Hold 的起始时间不能在缓冲区内
  - 但 Hold 可以跨越缓冲区（起始时间 < T-500ms，holdEnd > T）
  - 缓冲区只限制音符的"出现"时间，不限制"持续"时间
```

---

## 8. 按键映射完整规范

### 8.1 默认映射表

```
全程仅使用 D F J K 四个物理键

cols=1: F → col0
cols=2: F → col0, J → col1
cols=3: D → col0, F → col1, J → col2
cols=4: D → col0, F → col1, J → col2, K → col3
cols=5: 列滚动（阶段二），4键映射到5列中的4列活跃窗口
cols=6: 列滚动（阶段二），4键映射到6列中的4列活跃窗口
```

### 8.2 列滚动映射规则（阶段二）

```
活跃窗口 [windowStart, windowStart+3]

D → windowStart+0
F → windowStart+1
J → windowStart+2
K → windowStart+3

滚动触发：approach窗口内的音符超出当前活跃窗口
滚动期间：禁止判定，200ms动画
滚动完成：更新映射，恢复输入
```

### 8.3 可配置性预留

```
未来扩展：
  - KeyMapping 配置文件（INI格式）
  - 预设方案：Centered / LeftBias / RightBias
  - 自定义：玩家指定每个列对应的物理键
  - 当前阶段硬编码 Centered 方案
```
