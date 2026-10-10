# ZzClawTerm 集成 ZzTermCore 总体设计（弃用 ZzTermWidget）

- 日期：2026-10-10
- 仓库：ZzClawTerm（master）；ZzTermCore（contour，tag m17b 起）
- 前置：M13 spike 链路验证、M17b 不换行显示模式、ZzTermCore CI 7/7 绿

## 1. 目的与范围

用 ZzTermCore（纯 C++20 终端引擎）替换 qtermwidget 系的
ZzTermWidget（fork 的 fork），彻底弃用后者。直接重写
ZzTerminalView（ZzTermWidget 胶水层），不设并存开关——半成品期
终端区不可用是可接受的过渡态（用户明判）。

本规格是**总体设计（程序规格）**：定架构、主从、清单与验收口径。
每个特性收口单元各自走「规格 → 计划 → SDD」小里程碑（参照
ZzTermCore M13-M17 惯例）。

范围内：

- ZzTermCore 进主工程（third_party 子模块 + add_subdirectory）
- ZzCoreTerminalWidget（Qt Widget 前端，spike 升格）+ ZzTerminalView
  内部换血
- 历史主从收编（Core 内存历史为显示权威，ZzLogEngine 退为持久化
  + FTS5 搜索层）
- ptyqt 退役（ZzLocalPtyTransport 换 ZzTerm::Pty）
- 编码转码层（传输层前置，GBK 等非 UTF-8 保留）
- 特性对齐清单逐项收口（§5）+ 测试基座重写
- 终态：删 third_party/ZzTermWidget 子模块（含 vendored ptyqt）

范围外：

- contour 后端消费（ZzClawTerm 只用 Native 后端）
- ZzTermCore 引擎语义改动（发现缺口走 Core 小里程碑另行立项）
- Rust 版远端项目（与本仓库无关，远程已改名 ZzClawTerm-CPP）
- Sixel/Kitty 图形、块/列选、kitty 键盘协议等 fork 增强（YAGNI，
  有真实需求再立项）

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | 切入方式 | 直接重写 ZzTerminalView（QTermWidget 胶水层），不并存；半成品期不可用可接受 |
| 2 | ZzTermCore 进主工程 | third_party/ZzTermCore 子模块 + add_subdirectory（contour OFF、静态链接，spike 已实证）；安装态 find_package 等 API 冻结后再议 |
| 3 | 历史主从 | Core 内存 Scrollback 为显示权威；ZzLogEngine（热环/mmap 温层/SQLite+FTS5 冷层）退为持久化 + 全文搜索层；滚动越顶经 Core prependPrimaryLines 读回注入（纯文本、丢样式 = 既有口径） |
| 4 | 编码 | 传输层前置转码（收：编码→UTF-8 喂引擎；发：反向），GBK 等非 UTF-8 保留。引擎层与 contour 一致纯 UTF-8（已实证 contour 无字节编码转码层，其 charset 代码均为 DEC G0/G1 指定） |
| 5 | 验收口径 | 现状实际特性清单逐个收口（勘察报告 2026-10-10），每项 QTest + 用户手测；不对齐 qtermwidget 全部内建特性 |

## 3. 架构

### 3.1 分层

```
ZzTabManager / ZzSplitContainer（会话管理，零改动目标）
        │ 仅经 ZzTerminalView 既有接口
ZzTerminalView（换血：内部不再持 QTermWidget）
        │ 持有
ZzCoreTerminalWidget（新 Qt Widget 前端；spike ZzCoreViewWidget 升格）
        │ 消费只读契约
ZzTerminal（ZzTermCore）：RenderView / UnwrapView / HistoryView / Input
        │ feed / sendKey·sendText / resize
ZzTransportInterface（既有抽象不动）── ZzSshTransport（不动）
                                  └─ ZzLocalPtyTransport（ptyqt → ZzTerm::Pty）
```

转码层在 ZzTerminalView ↔ transport 边界（收方向：transport 字节 →
QStringDecoder 指定编码 → UTF-8 → term.feed；发方向：output 通道
UTF-8 → QStringEncoder → transport->write）。UTF-8 会话零开销直通。

### 3.2 ZzCoreTerminalWidget 职责（升格自 spike）

渲染（RenderView/UnwrapView 双模式）、配色（§3.3）、键盘 +
IME preedit、滚动条/滚轮、选区（含不换行完整长行）、右键菜单、
缩放、搜索高亮、铃铛/标题/链接信号转发。全量重绘起步，增量脏行
（RenderView rowDirty/dirtyRange）作为后续优化（YAGNI 登记）。

### 3.3 配色

Core 刻意不解析调色板（Cell.h:36「调色板解析由 Renderer 负责」）：
ZzCellView.foreground/background 为 Default/Indexed/Rgb 三类。前端
新建 `ZzTerminalPalette`（ZzClawTerm 自有）：ANSI 16 + bright 索引
→ RGB 的映射表 + 默认前景/背景，配色方案从 qtermwidget 资源格式
（`/lib/qtermwidget` 的 .colorscheme）移植为自有资源，方案名口径
保持（settings.ini 默认值 "Linux" 等不受影响）；四处消费点
（设置页/SSH 配置页/菜单主题子菜单/ZzTerminalView）的
`availableColorSchemes()` 由 ZzTerminalPalette 等价物承接。

### 3.4 历史主从

- **显示权威**：Core 内存 Scrollback（HistoryView）；滚动/搜索/选区
  全部走 Core 坐标（ZzLogicalPos 逻辑行空间）。
- **持久层**：ZzLogEngine 三层照旧（输出镜像落盘、FTS5 全文搜索、
  降级链不动）。
- **读回注入**：滚动越顶（Core 历史耗尽还要往上）时从 LogEngine
  读回纯文本行，经 Core 既有 prependPrimaryLines 注入屏幕顶部
  （M16 已备接口），丢样式为既有口径（fork 注释自认）。
- ZzScrollbackBridge 重写为「LogEngine ↔ prependPrimaryLines」桥；
  fork 私有注入点（dupDisplayOutput/setHistoryProvider）随
  ZzTermWidget 一并退役。

### 3.5 ptyqt 退役

ZzLocalPtyTransport 从 ptyqt 换到 ZzTerm::Pty（Core 自带 Unix PTY +
Windows ConPTY，M12 落地、CI 全绿）。注意现有兼容代码
（ConPty/Unix 通知机制差异，ZzLocalPtyTransport.cpp:57-79）在
ZzTerm::Pty 侧的等价口径，收口时逐条核对。ZzTermWidget 子模块的
vendored ptyqt 随之删除。

## 4. 构建并入

- third_party/ZzTermCore 子模块（pin contour 分支 m17b 起）；
  根 CMakeLists add_subdirectory，ZZTERM_WITH_CONTOUR=OFF、
  ZZTERM_BUILD_TESTS=OFF（spike 静态消费约束实证）；主工程
  cmake 最低版本抬到 3.26（ZzTermCore 要求，spike 已抬过）。
- CI 三平台（ci.yml ubuntu/windows/macos）新增子模块拉取与构建；
  spike 的 probe 冒烟范式升格为主工程集成冒烟。

## 5. 特性对齐清单（逐个收口，优先级序）

| # | 特性 | 来源 | 备注 |
| --- | --- | --- | --- |
| 1 | 渲染 + 配色 | spike 升格 + ZzTerminalPalette 新写 | 第一刀，打通即手测 |
| 2 | 键盘 + IME preedit | spike 已有键盘；IME 新写 | WA_InputMethodEnabled + preedit 绘制 |
| 3 | 滚动条/滚轮 + 历史读回注入 | spike 已有滚动；读回桥新写 | §3.4 主从 |
| 4 | 选区复制 + 右键菜单 | spike 已有选区；菜单新写 | 含不换行模式完整长行 |
| 5 | 字体/字号 + Ctrl+滚轮缩放 | 新写 | 设置页字号既有口径承接 |
| 6 | 搜索栏 | 新写 UI：Core M5b 内存区 + LogEngine FTS5 冷层 | 现状库内搜索栏未接线，此为新增能力 |
| 7 | 铃铛/标题变化/OSC 8 链接 | 新写信号转发 | 标签标题、窗口标题接线 |
| 8 | 鼠标上报 | 新写（vim/htop 需要） | X10/normal/button-event/any-event |
| 9 | 最小网格约束等价物 | 新写 | fork setMinimumTerminalSize（24x6）防护的等价逻辑 |
| 10 | 转码层 | 新写 | §2 决策 4 |

每项收口 = 单元测试（QTest）+ 用户手测确认。

## 6. 测试基座重写

6 个直构造 QTermWidget 的测试文件（tst_ZzTerminalView /
tst_ZzScrollbackBridge / tst_ZzSettingsPage / tst_ZzConnectFlow /
tst_ZzSplitContainer / tst_PerfScrollback）驱动方式整体重写为新
前端/引擎契约；spike QTest（offscreen）与 probe 协议为范式。

## 7. 文档同步

- 本仓库 docs/：架构文档补前端分层（§3.1）与历史主从（§3.4）；
- 每特性收口的子里程碑规格/计划按 ZzTermCore 惯例入
  docs/superpowers/；
- 删除 ZzTermWidget 时同步 .gitmodules、README、BUILDING.md。

## 8. 里程碑序列（程序级）

- **Z1 骨架换血**：子模块并入 + ZzCoreTerminalWidget 最小渲染/键盘
  + ZzTerminalView 换血 + ptyqt 退役 + 转码层——链路可用（特性 1
  基础、2 键盘、10）；
- **Z2 显示特性**：配色、IME、滚动+读回桥、选区+右键菜单、缩放
  （特性 1 完整、2-5、9）；
- **Z3 会话体验**：搜索栏、铃铛/标题/链接、鼠标上报（特性 6-8）；
- **Z4 收尾**：测试基座重写完成 + 删 ZzTermWidget/ptyqt + CI 全绿 +
  文档收官。

每个 Zn 内含若干收口单元，各走独立规格→计划→SDD。
