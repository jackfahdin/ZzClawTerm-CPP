# Z2 显示特性收口设计

- 日期：2026-10-12
- 仓库：ZzClawTerm（master）
- 前置：Z1 骨架换血已收官（fabc421 + e8ffa02）；总体设计
  `2026-10-10-zztermcore-integration-design.md` §8 定 Z2 范围；
  本规格为 Z2 程序级设计，8 个收口单元各走「计划 → SDD」。

## 1. 目的与范围

收口 Z1 换血后终端区的显示特性缺口，让用户可感知的终端行为
（配色、中文输入、历史读回、选区菜单、缩放、尺寸约束、滚轮）
完整可用。

范围内（8 个收口单元）：

1. ZzTerminalPalette 配色（iTerm2-Color-Schemes 全量替换）
2. Qt5Compat 引入 + GBK 等东亚编码激活
3. IME preedit（中文输入）
4. 历史读回注入桥 + reflow 对账复位
5. 选区收口 + 右键菜单四件套
6. 字体/字号 + Ctrl+滚轮缩放 + 隐藏标签页字号滞留修复
7. 最小网格约束（24×6 等价物）
8. Alternate 屏滚轮转方向键

范围外：

- 搜索栏、铃铛/标题/OSC 8 链接、鼠标上报 → Z3
- 测试基座重写（4 个摘除测试）、删 ZzTermWidget/ptyqt → Z4
- ZzTermCore 引擎语义改动（发现缺口走 Core 小里程碑另行立项）
- qtermwidget 旧配色方案名兼容（用户明判：项目早期，不背历史包袱）

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | Z2 边界 | 总体设计 §8 的 Z2 清单 + Qt5Compat/GBK 并入（8 项全收） |
| 2 | 配色方案来源 | iTerm2-Color-Schemes（MIT）全量替换 qtermwidget 方案，不做旧名映射 |
| 3 | 打包范围 | 全量 300+ 方案打进 Qt 资源（单文件 1-2KB，总量可忽略） |
| 4 | 选择入口形态 | 下拉框全量（QComboBox 键入过滤）+ 菜单主题子菜单保持代码内 ~10 个精选名单 |
| 5 | 旧配置兼容 | 不做映射；读到未知名一律静默回退新默认方案 |
| 6 | 右键菜单 | 复制（有选区时可用）/ 粘贴 / 全选 / 分隔 / 清空屏幕 |
| 7 | 组织方式 | 一个 Z2 计划、8 任务按依赖排序、SDD 顺序执行（多数单元碰同一文件，不并行分支） |

## 3. 组织与任务序

| 序 | 收口单元 | 主要落点 | 依赖 |
| --- | --- | --- | --- |
| 1 | ZzTerminalPalette 配色 | 新增 palette 单元 + qrc 资源 + 四处消费点接线 + 渲染消费 | 无 |
| 2 | Qt5Compat + GBK 激活 | CMake + ZzTerminalView + 解 QSKIP | 无 |
| 3 | IME preedit | ZzCoreTerminalWidget | 无 |
| 4 | 历史读回注入桥 + reflow 对账 | ZzScrollbackBridge 重写 | 无 |
| 5 | 选区 + 右键菜单 | ZzCoreTerminalWidget | 1 |
| 6 | 缩放 + 字号滞留 | ZzCoreTerminalWidget + ZzTerminalView | 1 |
| 7 | 最小网格约束 | ZzCoreTerminalWidget | 6 |
| 8 | Alt 屏滚轮转方向键 | ZzCoreTerminalWidget | 无 |

每单元 = 独立计划任务 + QTest + 用户手测确认，收口一个进下一个。

## 4. 各单元设计

### 4.1 ZzTerminalPalette（任务 1）

- 新单元 `src/terminal/ZzTerminalPalette.{h,cpp}`：解析 Konsole
  .colorscheme（INI 格式，QSettings 可读）→ 提供「方案名列表 /
  按名取 16+bright ANSI 索引色 + 默认前景/背景」纯查询接口；
  零 Qt Widget 依赖，可独立 QTest。
- Vendoring：iTerm2-Color-Schemes 上游 `konsole/*.colorscheme`
  全量快照拷贝进 `src/terminal/colorschemes/`（入 qrc 资源），
  附 `LICENSE`（MIT）与 `PROVENANCE.md`（上游 commit 哈希 +
  快照日期）。不用子模块（上游仓库含大量截图，整体过重）。
- 新默认方案：settings.ini 默认值 "Linux" 改为 "Tango Dark"
  （高对比深色，最接近旧观感）；读到未知名静默回退默认。
- 消费接线：
  - `ZzAppSettings::availableColorSchemes()` 委托
    ZzTerminalPalette；
  - 设置页 / SSH 配置页下拉框全量（QComboBox 字母序）；
  - 菜单主题子菜单（ZzMenuBarService）换为代码内写死的 ~10 个
    精选名单，名单外方案不出现但下拉框可选；
  - 渲染侧 ZzCoreTerminalWidget 硬编码调色板（现 :56 注释处）
    换为按当前方案名查表，方案切换即时生效。

### 4.2 Qt5Compat + GBK 激活（任务 2）

- 根/src CMake：find Qt6::Core5Compat，链接 src 目标；
  `ZzTerminalView.cpp:191` 处编码回退逻辑不变——Qt5Compat 在场
  时 GBK 等东亚编码 `encodingForName` 自然命中。
- `tst_ZzTerminalView.cpp:59` 解 QSKIP，恢复字节级断言。
- Windows/macOS CI 一并验证 Core5Compat 可用。

### 4.3 IME preedit（任务 3）

- `WA_InputMethodEnabled` + 重写 `inputMethodEvent`：
  - preedit 串在光标处即时绘制（下划线样式，颜色跟随当前方案）；
  - commit 串走既有 `sendText` 通道；
  - `inputMethodQuery` 报光标微焦点矩形（输入法候选窗定位）。
- QTest 用 `QInputMethodEvent` 直造事件覆盖 preedit/commit 路径。

### 4.4 历史读回注入桥 + reflow 对账（任务 4）

- `ZzScrollbackBridge` 重写为「LogEngine ↔ prependPrimaryLines」桥：
  - 视口顶行逻辑行越顶（Core 内存历史耗尽还要往上）时，从
    ZzLogEngine 读回更早的纯文本行，按块（200 行/次）经
    `prependPrimaryLines` 注入屏幕顶部，并补偿视口滚动位置；
  - 丢样式为既有口径（fork 注释自认，总体设计 §3.4）。
- reflow 对账复位：订阅引擎 `generation()`，列宽变化导致历史
  重组时，桥内「已注入行数账本」整体清零重新锚定——宁可重复
  注入，不可差分错位。
- 若 generation 语义不足，走 ZzTermCore 小里程碑另行立项，
  不在 Z2 顺手改引擎。
- **勘误 E-1**（2026-10-12 计划期核实）：原假设读回注入用既有
  `prependPrimaryLines`，其语义实为「屏幕顶部插入、溢出裁回历史
  最新端」（M16b 接缝归还专用），用于读回会顺序倒置；
  ZzScrollback 无最旧端注入公共 API。定案：Z2 任务 5 前置
  ZzTermCore M19 原语里程碑（`prependHistoryLines`：历史最旧端
  注入 + droppedLineCount 同步 +N），验收标准见计划任务 5。

### 4.5 选区收口 + 右键菜单（任务 5）

- 选区：补齐不换行模式完整长行复制回归测试（spike 期已修
  80b8f03/922edcc/14c825c 三案的回归固化）。
- 右键菜单（`contextMenuEvent` 就地构建 QMenu）：
  复制（选区存在才可用）/ 粘贴 / 全选 / 分隔线 / 清空屏幕；
  动作接 ZzCoreTerminalWidget 既有公开方法，无新状态。

### 4.6 字体/字号 + 缩放 + 字号滞留（任务 6）

- 设置页字号既有口径承接（ZzAppSettings 读存不动）；
- Ctrl+滚轮逐级缩放（上下限钳制，如 6pt–72pt），缩放即时重算
  网格并通知引擎 resize；
- 隐藏标签页字号滞留修复：标签切回可见时（showEvent/activate
  路径）强制以当前设置值重算网格与字体度量，杜绝后台标签滞留
  旧字号。

### 4.7 最小网格约束（任务 7）

- `sizeHint`/resize 路径钳制最小 24 列 × 6 行网格；窗口小于
  该网格时拒绝继续 shrink（等价 fork setMinimumTerminalSize
  防护语义）。

### 4.8 Alternate 屏滚轮转方向键（任务 8）

- 引擎处于 alternate screen 且未开鼠标上报时，滚轮事件转为
  ↑/↓ 方向键序列（每滚轮档 = 3 行）发给会话；vim/less 等全屏
  程序滚轮翻行。primary screen 或鼠标上报开启时行为不变。

## 5. 错误处理

- 配色：方案文件解析失败/字段缺失 → 对应字段回退默认方案值，
  不崩；`availableColorSchemes()` 恒含默认方案。
- 转码：编码名无效 → 回退 UTF-8（Z1 口径不动）。
- 读回桥：LogEngine 读回失败/无更早历史 → 静默停在越顶处，
  不弹错、不注入假行。
- 其余单元无外部失败面，遵循 Qt 事件默认语义。

## 6. 测试

- 每单元独立 QTest（offscreen，spike 范式）：palette 解析与
  回退、GBK 字节级往返、IME preedit/commit、读回注入与
  reflow 复位对账、菜单动作可用性、缩放钳制与字号滞留回归、
  最小网格钳制、Alt 屏滚轮方向键序列。
- 既有基线 50/50 全程不红；新增测试逐一入 ctest 注册。

## 7. 验收

- 单元收口 = 测试绿 + 用户手测确认（配色切换、中文输入、
  滚动越顶读回、resize 后历史不错位、右键四件套、Ctrl+滚轮
  缩放、最小窗口不崩、vim/less 滚轮翻行）。
- Z2 收官 = 8 单元全收 + push + CI 绿 + tag。
