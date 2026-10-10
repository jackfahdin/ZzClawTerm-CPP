# Z1 骨架换血设计（ZzClawTerm × ZzTermCore）

- 日期：2026-10-10
- 前置：总体设计（2026-10-10-zztermcore-integration-design.md，071ce65）
- 仓库：ZzClawTerm（master）；ZzTermCore 子模块 pin contour 分支（m17b）

## 1. 目的与范围

打通「ZzTermCore 引擎 + 新 Qt 前端」的最小可用链路：ZzClawTerm
本地 bash 与 SSH 会话能开、能显示、能输入、能缩拉。ZzTermWidget
（qtermwidget fork）从构建摘除，ptyqt 退役。

范围内：

- third_party/ZzTermCore 子模块并入主工程构建
- ZzCoreTerminalWidget（新 Qt Widget 前端，spike 升格）：
  渲染（默认调色板配色）/键盘/滚动/选区/不换行模式
- ZzTerminalView 内部换血（对外接口不变）
- ZzLocalPtyTransport：ptyqt → ZzTerm::Pty
- 传输边界编码转码层（GBK 等非 UTF-8 保留）
- 历史落盘换源（dupDisplayOutput → HistoryView 代计数差分）
- tst_ZzTerminalView 重写；其余 5 个 QTermWidget 直构造测试摘除登记
- CI 三平台子模块拉取

范围外（各归其里程碑）：

- 配色方案表与设置页接线（Z2 特性 1 完整化）
- IME preedit、右键菜单、缩放（Z2）
- 历史读回注入桥（Z2 特性 3）
- 搜索栏、铃铛/标题/链接、鼠标上报（Z3）
- 删 third_party/ZzTermWidget 文件本体与 fork 私有 API 清理（Z4）
- spike/ 目录保持不动（诊断件）
- ZzTermCore 引擎改动（发现缺口另行立项）

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | 历史落盘过渡期 | 换源保留：落盘源从 fork 私有 dupDisplayOutput 改为 Core scrollout 定稿行（feed 后 HistoryView 代计数/行数差分），语义更干净（无重绘噪音），Core 零改动 |
| 2 | Z1 配色 | 内置默认调色板（移植 qtermwidget 默认方案为硬编码默认），方案表与设置页接线留 Z2 |
| 3 | QTermWidget 直构造测试 | 拆处置：Z1 只重写 tst_ZzTerminalView；其余 5 个（tst_ZzScrollbackBridge/tst_ZzSettingsPage/tst_ZzConnectFlow/tst_ZzSplitContainer/tst_PerfScrollback）从构建摘除并登记，随特性收口逐个重写回 |
| 4 | 验收手势 | 本地 bash + SSH localhost 会话、显示/键盘/缩拉/不换行正常、probe 冒烟进 CI；无配色选择/IME/搜索栏属预期 |

## 3. 构建并入

- `.gitmodules` 增 third_party/ZzTermCore（url 用与既有子模块同
  风格的 ssh 地址，CI 沿用 ssh→https 重写）；pin contour 分支
  m17b（f1c54fc）。
- 根 CMakeLists.txt：`cmake_minimum_required` 抬 3.26；
  `add_subdirectory(third_party/ZzTermCore)`，前置
  `ZZTERM_WITH_CONTOUR=OFF`、`ZZTERM_BUILD_TESTS=OFF`（静态消费
  约束，spike 实证）。
- `src/CMakeLists.txt`：ZzClawTermApp 的 PUBLIC 链接由
  `ZzQTermWidget::qtermwidget` 换为 `ZzTerm::ZzTermCore` +
  `ZzTerm::Pty`；删 `ZZQTERMWIDGET_*` 选项与
  `add_subdirectory(third_party/ZzTermWidget)`。
- CI（ci.yml 三平台）：子模块拉取清单加 ZzTermCore。

## 4. ZzCoreTerminalWidget（src/terminal/ 新建）

spike ZzCoreViewWidget 升格，保留其已验证链路并补配色：

- 渲染：RenderView 逐行 cellAt，cell 的 fg/bg（Default/Indexed/Rgb）
  经内置默认调色板解析为 QColor 绘制（背景格填充 + 文本）；
  默认调色板 = qtermwidget 默认方案（"Linux"）的 16+bright 索引
  表与默认前景/背景，硬编码常量表（Z2 再抽 ZzTerminalPalette）。
- 键盘/滚动/选区/不换行（Ctrl+Shift+U）：spike 现成实现搬入
  （含三个手测修复：锚点自存重放、高亮画笔收口、端点粒度）。
- 光标绘制、Alternate 屏滚动禁用、键盘输入回底等 spike 语义保留。
- 增量脏行优化不做（YAGNI，全量重绘起步）。

## 5. ZzTerminalView 换血

对外契约不变（构造、signals、被 ZzTabManager/测试调用的方法），
内部：

- QTermWidget 实例 → ZzCoreTerminalWidget（持 ZzTerminal，
  Native 后端，初始 80x24、历史 10000 行——对齐现状
  setHistorySize 默认值）。
- 键盘字节：widget output 通道 →（转码层）→ transport->write。
- transport->dataReceived →（转码层）→ term.feed → update。
- 尺寸：widget 像素→网格换算 → term.resize + 既有
  ZzResizeDebouncer（150ms 尾随）→ transport->resize；初始行列
  供给 ZzTabManager 的 screenColumnsCount/screenLinesCount 由
  widget 等价接口承接。
- 设置应用：字号/编码/配色签名保留；字号与编码生效，配色暂固定
  默认（Z2 接方案表）；setHistorySize → Core 构造参数（重建终端
  实例的语义与现状等价性在计划阶段核对）。
- termWidget() 返回值改为新 widget 指针；其消费点（历史桥挂载、
  测试）同步改。

## 6. ptyqt 退役（ZzLocalPtyTransport）

ptyqt（iptyprocess）→ ZzTerm::Pty：spawn（ZzPtyConfig：argv、
行列、rawMode=false 交互口径）、masterFd 置 O_NONBLOCK +
QSocketNotifier 读循环、writeAll、resize、EOF 检测。
spike main.cpp --local 链为同构参照。现状 ConPty/Unix 通知差异
兼容代码（ZzLocalPtyTransport.cpp:57-79）逐条核对 ZzTerm::Pty
侧等价物（M12 ConPTY 已落地）。Windows 侧行为经 CI 验证。

## 7. 转码层

ZzTerminalView 收发边界（新私有辅助，QStringEncoder/
QStringDecoder）：

- 收：transport 字节 → 按会话编码 decode → UTF-8 → term.feed；
- 发：output 通道 UTF-8 → encode → transport->write；
- UTF-8（默认）直通零开销；编码来源沿用既有设置（settings.ini
  口径不变）。

## 8. 历史落盘换源

ZzScrollbackBridge 改造（读回注入不在本期）：

- 删除 dupDisplayOutput 依赖；改为 ZzTerminalView 在 feed 后比较
  HistoryView 代计数：lineCount 增长部分（scrollout 定稿行）经
  historyView().lineAt() 取纯文本追加 ZzLogEngine.appendLine；
  droppedLineCount 用于容量裁剪对账。
- 落盘内容口径：纯文本、无样式（同现状）。
- 降级链（degradedToMemoryOnly/degradedToWarmOnly）不动。

## 9. 测试与 CI

- 重写 `tests/unit/tst_ZzTerminalView.cpp`：胶水契约——
  feed 字节 → widget 渲染文本断言；sendKey/sendText → transport
  写出字节断言；resize → 去抖后 transport->resize 断言；
  转码层 GBK 往返断言。
- 摘除 5 个 QTermWidget 直构造测试（CMake 注释 + 登记表，
  文件保留）。
- 摘除后 CI 三平台构建 + 剩余单测全绿；probe 式冒烟范式留 Z2+
  引入主工程（Z1 只做单测）。
- ZzTermCore 侧零改动预期；五项基线（ZzTermCore 仓库）不受影响。

## 10. 验收（用户手测）

`ZzClawTerm` 主程序：本地 bash 会话与 SSH localhost 会话——
显示正确（默认配色）、键盘输入、缩拉窗口内容完整（M17e 手感）、
不换行模式（Ctrl+Shift+U）与横向滚动、选区复制完整长行。
已知预期内缺失：配色选择不生效、无 IME、无右键菜单/缩放/搜索栏、
历史读回不注入（只落盘）。

## 11. 文档同步

- 本规格入 docs/superpowers/specs/；计划入 docs/superpowers/plans/；
- README/BUILDING.md 的子模块与构建说明在 Z1 落地 commit 同步；
- 摘除测试登记入计划文件。
