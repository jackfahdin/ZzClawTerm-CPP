# Z1 骨架换血实现计划（ZzClawTerm × ZzTermCore）

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** ZzTermCore 子模块并入 ZzClawTerm 主工程，ZzTerminalView 内部从 QTermWidget 换血到 ZzTermCore + 新 Qt 前端 ZzCoreTerminalWidget，ptyqt 退役，传输边界转码层落地——本地 bash 与 SSH 会话链路可用。

**架构：** 新前端 ZzCoreTerminalWidget（spike ZzCoreViewWidget 升格 + 配色）持 ZzTerminal（Native 后端）消费 RenderView/UnwrapView 只读契约；ZzTerminalView 对外接口不变只做胶水；ZzLocalPtyTransport 换 ZzTerm::Pty；转码在 view 收发边界（QStringDecoder/Encoder）；历史落盘源从 fork 私有 dupDisplayOutput 换成 HistoryView 代计数差分。

**技术栈：** C++20、CMake（preset）、Qt6 6.8+（Widgets/Test）、ZzTermCore（contour 分支 m17b，f1c54fc）、QTest offscreen。

**仓库纪律：** 本计划在 ZzClawTerm 仓库（/home/zz/Jackfahdin/github/ZzClawTerm，master）执行；commit `type(scope): 中文描述`；ZzTermCore 仓库零改动（发现缺口停下来上报，不修 Core）。

---

## 文件结构

- 修改：`.gitmodules`（新增 ZzTermCore 子模块条目）
- 新增子模块：`third_party/ZzTermCore`（pin f1c54fc）
- 修改：`CMakeLists.txt`（cmake 3.26 + ZzTermCore add_subdirectory）
- 修改：`src/CMakeLists.txt`（链接换 ZzTerm::ZzTermCore + ZzTerm::Pty）
- 创建：`src/terminal/ZzCoreTerminalWidget.h` / `.cpp`（新前端）
- 修改：`src/terminal/ZzTerminalView.h` / `.cpp`（换血 + 转码层）
- 修改：`src/transport/ZzLocalPtyTransport.cpp`（ptyqt → ZzTerm::Pty；.h 不动）
- 修改：`src/terminal/ZzScrollbackBridge.h` / `.cpp`（落盘换源）
- 修改：`src/tab/ZzTabManager.cpp:59-60,371-372,464-465`（screenColumnsCount/screenLinesCount 换等价接口）
- 重写：`tests/unit/tst_ZzTerminalView.cpp`（胶水契约）
- 创建：`tests/unit/tst_ZzCoreTerminalWidget.cpp`
- 修改：`tests/unit/tst_ZzScrollbackBridge.cpp`（落盘换源后重写——任务 5）
- 摘除登记（任务 4，CMake 注释）：`tst_ZzSettingsPage.cpp`、`tst_ZzConnectFlow.cpp`、`tst_ZzSplitContainer.cpp`、`tests/perf/tst_PerfScrollback.cpp`（随特性收口逐个重写回；tst_ZzScrollbackBridge 任务 5 即重写回）
- 修改：`README.md` / `BUILDING.md`（子模块清单与构建说明）

## 基线

任务 1 第一步先实测并记录：当前 master（6223c47）全量
`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
的测试总数（记入任务 1 报告，作为后续任务的回归基线）。

## 全局约束

- **编译原子性**：摘除 `ZzQTermWidget::qtermwidget` 链接的 commit
  必须同时让所有引用 qtermwidget 符号的编译单元脱离构建（view
  换血 + 5 个测试摘除 + ZzTabManager 换接口），否则编译断。
- **转码层口径**：收方向 transport 字节 → QStringDecoder（会话
  编码）→ UTF-8 → term.feed；发方向 output 通道 UTF-8 →
  QStringEncoder → transport->write；UTF-8 会话同样过 codec
  （无特判，行为一致优先）。
- **ZzResizeDebouncer（150ms 尾随去抖）保留**，位于
  third_party/ZzSshCore/src/ZzResizeDebouncer.h。
- **spike/ 目录不动**（诊断件，独立工程不进主构建）。
- Windows 注意：ZzPty 的 ConPTY 侧是 HANDLE 管道，QSocketNotifier
  不可用——读取走 QThread 阻塞读（任务 3 给了完整代码）。

---

### 任务 1：ZzTermCore 子模块并入构建（并存期，行为零变化）

**文件：**
- 修改：`.gitmodules`、`CMakeLists.txt:1` 与 :30-35 区域、`src/CMakeLists.txt:84-93` 链接块

- [ ] **步骤 1：记录基线**

```bash
cd /home/zz/Jackfahdin/github/ZzClawTerm
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug -j$(nproc) && ctest --preset linux-gcc-debug 2>&1 | tail -3
```

记录通过数（写入报告）。若基线本身不绿，BLOCKED 上报。

- [ ] **步骤 2：加入子模块**

```bash
git submodule add -b contour git@github.com:jackfahdin/ZzTermCore.git third_party/ZzTermCore
cd third_party/ZzTermCore && git checkout f1c54fc && cd ../..
git add .gitmodules third_party/ZzTermCore
```

- [ ] **步骤 3：CMake 接线**

`CMakeLists.txt`：
- 第 1 行改为 `cmake_minimum_required(VERSION 3.26)`
- ZzTermWidget 段（:30-35）**保留不动**（并存期），在其后插入：

```cmake
# ZzTermCore（终端引擎，Z1）：Native 后端静态消费，contour OFF
#（spike 实证的静态链接约束）；引擎自带测试不在本工程编译
#（其五项基线在 ZzTermCore 仓库自保）。
zz_require_submodule(third_party/ZzTermCore)
set(ZZTERM_WITH_CONTOUR OFF CACHE BOOL "" FORCE)
set(ZZTERM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/ZzTermCore)
```

`src/CMakeLists.txt` 链接块（ZzClawTermApp PUBLIC）追加两行
（qtermwidget 暂留）：

```cmake
        ZzTerm::ZzTermCore
        ZzTerm::Pty
```

- [ ] **步骤 4：验证并存编译 + 回归**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug -j$(nproc) && ctest --preset linux-gcc-debug 2>&1 | tail -3
```

预期：构建绿（ZzTermCore 静态库编入），测试数与基线一致。

- [ ] **步骤 5：CI 核实**

读 `.github/workflows/ci.yml` 的子模块拉取步骤（三处：约
:34-43/:187-196/:255-264）：`git submodule update --init --recursive`
为全量拉取，`git@github.com:` → `https://github.com/` 重写为通用
规则——新增 ZzTermCore 子模块**无需改 ci.yml**。在报告中确认此
结论（引用行号）。

- [ ] **步骤 6：Commit**

```bash
git add .gitmodules third_party/ZzTermCore CMakeLists.txt src/CMakeLists.txt
git commit -m "build(z1): ZzTermCore 子模块并入主工程构建（contour OFF 静态，并存期）"
```

### 任务 2：ZzCoreTerminalWidget（新前端，纯新增）

**文件：**
- 创建：`src/terminal/ZzCoreTerminalWidget.h`、`src/terminal/ZzCoreTerminalWidget.cpp`
- 测试：创建 `tests/unit/tst_ZzCoreTerminalWidget.cpp`；修改 `tests/CMakeLists.txt`（注册）
- 移植原料：`spike/ZzCoreViewWidget.h` / `.cpp`（本仓库内，已含全部已验证链路）

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/tst_ZzCoreTerminalWidget.cpp`：

```cpp
#include <QtTest/QtTest>

#include <QClipboard>

#include "terminal/ZzCoreTerminalWidget.h"

/**
 * @brief ZzCoreTerminalWidget（Z1）契约：feed→渲染、键盘→sendData、
 * resize→gridResized、配色非默认色绘制、选区复制、不换行模式。
 */
class tst_ZzCoreTerminalWidget : public QObject
{
    Q_OBJECT
private slots:
    void feedRendersText()
    {
        ZzCoreTerminalWidget widget;
        widget.recvData(QByteArray("hello z1\r\n"));
        QCOMPARE(widget.visibleLineTextForTest(0), QString("hello z1"));
    }

    void keyboardSendsBytes()
    {
        ZzCoreTerminalWidget widget;
        QSignalSpy spy(&widget, &ZzCoreTerminalWidget::sendData);
        QTest::keyClicks(&widget, "ls");
        QTest::keyClick(&widget, Qt::Key_Return);
        QVERIFY(spy.count() >= 1);
        QByteArray all;
        for (const auto &call : spy)
            all += call.at(0).toByteArray();
        QCOMPARE(all, QByteArray("ls\r"));
    }

    void resizeEmitsGrid()
    {
        ZzCoreTerminalWidget widget;
        QSignalSpy spy(&widget, &ZzCoreTerminalWidget::gridResized);
        const int cw = widget.cellWidthForTest();
        const int ch = widget.cellHeightForTest();
        widget.resize(100 * cw, 30 * ch);
        QVERIFY(spy.count() >= 1);
        const auto last = spy.last();
        QCOMPARE(last.at(0).toInt(), 100);
        QCOMPARE(last.at(1).toInt(), 30);
    }

    void selectionCopiesCells()
    {
        ZzCoreTerminalWidget widget;
        widget.recvData(QByteArray("abcdef\r\n"));
        const auto &view = widget.terminal().unwrapView();
        const ZzLogicalPos p0 = {0, 0};
        const ZzLogicalPos p1 = {0, 3};
        widget.terminal().setSelection(p0, p1);
        QCOMPARE(QString::fromStdString(widget.terminal().selectedText()),
                 QString("abc"));
    }

    void unwrapModeStitchesChains()
    {
        ZzCoreTerminalWidget widget;
        widget.terminal().resize(10, 4);
        widget.recvData(QByteArray("xxxxxxxxxxxxxxxxxxxxxxxxx\r\n")); // 25 x
        widget.setUnwrapMode(true);
        QCOMPARE(widget.terminal().unwrapView().lineCount(), std::size_t(3));
        QCOMPARE(widget.maxHScrollOffset(), 25 - 10);
    }
};

QTEST_MAIN(tst_ZzCoreTerminalWidget)
#include "tst_ZzCoreTerminalWidget.moc"
```

`tests/CMakeLists.txt` 在 `zz_add_qtest(tst_ZzTerminalView ...)` 行
附近加：

```cmake
zz_add_qtest(tst_ZzCoreTerminalWidget unit/tst_ZzCoreTerminalWidget.cpp)
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug 2>&1 | tail -3
```

预期：编译失败（`terminal/ZzCoreTerminalWidget.h` 不存在）。

- [ ] **步骤 3：实现 ZzCoreTerminalWidget**

移植 spike/ZzCoreViewWidget（逐文件复制后按下述差异表改）。头文件
（完整给出）：

```cpp
#pragma once

/**
 * @file ZzCoreTerminalWidget.h
 * @brief Z1：ZzTermCore RenderView/UnwrapView 契约的 Qt Widget 前端
 * （spike ZzCoreViewWidget 升格 + 默认调色板配色）。
 *
 * 与 spike 原型的差异：
 * - 喂入口更名 recvData(QByteArray)（对齐 ZzTerminalView 胶水用语）；
 * - 键盘字节经信号 sendData(QByteArray) 发出（spike 是 writeFn 回调）；
 * - 渲染带配色：cell fg/bg 经内置默认调色板（qtermwidget "Linux"
 *   方案）解析为 QColor，背景格填充；
 * - 暴露 gridCols()/gridRows()（ZzTabManager 的 screenColumnsCount/
 *   screenLinesCount 等价物）与 cellWidthForTest/cellHeightForTest；
 * - 保留 spike 的：不换行模式（Ctrl+Shift+U）、选区（锚点自存重放、
 *   画笔收口、端点粒度+1、越缘步进）、留痕（ZZCORE_SPIKE_TRACE）。
 */

#include <QWidget>
#include <QString>

#include <cstdio>
#include <string_view>

#include <ZzTerm/Terminal.h>

/**
 * @brief ZzTermCore 引擎的 Qt Widget 前端（Z1 最小可用形态）。
 */
class ZzCoreTerminalWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ZzCoreTerminalWidget(QWidget *parent = nullptr);

    /** @brief 远端输出喂入（渲染方向唯一入口）。 */
    void recvData(const QByteArray &data);

    /** @brief 只读访问持有的终端（测试与视图层坐标换算用）。 */
    [[nodiscard]] const ZzTerminal &terminal() const noexcept { return term_; }
    /** @brief 可变访问（测试设置选区/尺寸用）。 */
    [[nodiscard]] ZzTerminal &terminal() noexcept { return term_; }

    /** @brief 当前网格列数/行数（screenColumnsCount/screenLinesCount 等价物）。 */
    [[nodiscard]] int gridCols() const noexcept { return term_.size().cols; }
    [[nodiscard]] int gridRows() const noexcept { return term_.size().rows; }

    /** @brief 不换行显示模式开关（M17b；Ctrl+Shift+U 亦切换）。 */
    void setUnwrapMode(bool on);
    [[nodiscard]] bool unwrapMode() const noexcept { return unwrapMode_; }

    /** @brief 横向滚动（不换行模式）。 */
    void setHScrollOffset(int offset);
    [[nodiscard]] int hScrollOffset() const noexcept { return hScrollOffset_; }
    [[nodiscard]] int maxHScrollOffset() const noexcept;

    /** @brief 纵向滚动偏移（0 = 底部最新）。 */
    void setScrollOffset(int offset);
    [[nodiscard]] int scrollOffset() const noexcept { return scrollOffset_; }
    [[nodiscard]] int maxScrollOffset() const noexcept;

    /** @brief 测试钩子：可见第 row 行文本（物理模式）。 */
    [[nodiscard]] QString visibleLineTextForTest(int row) const;
    /** @brief 测试钩子：不换行模式可见第 row 行横向窗口文本。 */
    [[nodiscard]] QString visibleStitchedTextForTest(int row) const;
    /** @brief 测试钩子：单格像素宽/高。 */
    [[nodiscard]] int cellWidthForTest() const noexcept { return cellWidth_; }
    [[nodiscard]] int cellHeightForTest() const noexcept { return cellHeight_; }

    /** @brief 设置终端字号（pt）。 */
    void setTerminalFontPointSize(int pointSize);

signals:
    /** @brief 键盘编码字节（终端→传输方向）。 */
    void sendData(const QByteArray &data);
    /** @brief 网格行列变化（宿主据此去抖转发 transport->resize）。 */
    void gridResized(int cols, int rows);
    /** @brief 纵向可滚动范围/偏移变化（滚动条接线用，Z2 接滚动条）。 */
    void scrollRangeChanged(int maximum);
    void scrollOffsetChanged(int offset);
    /** @brief 横向可滚动范围/偏移变化。 */
    void hScrollRangeChanged(int maximum);
    void hScrollOffsetChanged(int offset);
    /** @brief 一次 recvData 的 feed 完成（历史落盘差分等观察者用）。 */
    void contentFed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // —— 以下成员与 spike ZzCoreViewWidget 同名同义（含三个手测修复）——
    void updateCellMetrics();
    void paintWrapped(QPainter &painter);
    void paintUnwrapped(QPainter &painter);
    void paintSelectionOnWrappedRow(QPainter &painter, int row);
    void paintSelectionOnStitchedRow(QPainter &painter, std::size_t idx,
                                     int row, const ZzStitchedLineView &line);
    [[nodiscard]] ZzLineView lineViewAtVisibleRow(int row) const;
    [[nodiscard]] ZzLogicalPos logicalPosAtPixel(QPoint pos) const;
    [[nodiscard]] std::size_t stitchedTotalLines() const;
    [[nodiscard]] int stitchedHistoryCount() const;
    void followCursorHorizontally();
    void traceFeed(std::string_view data);
    void traceResize(int cols, int rows);

    ZzTerminal term_;                 ///< 引擎（Native 80x24，历史 10000）
    int cellWidth_ = 1;
    int cellHeight_ = 1;
    int cellAscent_ = 0;
    bool unwrapMode_ = false;
    int hScrollOffset_ = 0;
    int scrollOffset_ = 0;
    bool selecting_ = false;
    ZzLogicalPos selAnchor_{};
    std::FILE *trace_ = nullptr;      ///< 留痕（env ZZCORE_SPIKE_TRACE）
};
```

.cpp 移植差异表（其余逐字搬 spike/ZzCoreViewWidget.cpp）：

1. 构造函数：`term_(80, 24, ZzBackendKind::Native, 10000)` 不变；删
   writeFn_/setOutputHandler，改为 output 通道转信号：
   ```cpp
   term_.setOutputHandler([this](std::string_view bytes) {
       emit sendData(QByteArray(bytes.data(), (qsizetype)bytes.size()));
   });
   ```
   留痕块保留逐字。
2. `feedForTest` 更名 `recvData(const QByteArray&)`（内部调
   term_.feed + 发射 contentFed() + followCursorHorizontally +
   scrollRangeChanged 差分 + hScrollRangeChanged 差分 + update，
   同 spike 逻辑）。
3. 键盘：`term_.sendText/sendKey` 不变（字节经 output 通道出）。
4. **配色渲染**（spike 是无色绘制，此处为新增）：paintWrapped/
   paintUnwrapped 的格绘制改为：
   ```cpp
   // 内置默认调色板（qtermwidget "Linux" 方案；Z2 抽 ZzTerminalPalette）
   // 取值来源：third_party/ZzTermWidget/lib/color-schemes/Linux.colorscheme
   // ——移植期从该文件转写 16+bright 表（实现者照抄数值），
   // 表结构：
   static const QColor kDefaultPalette[16] = { /* 0-7 普通 + 8-15 亮，
       从 Linux.colorscheme 转写 */ };
   // cell 颜色解析：
   static QColor resolveCellColor(ZzColor color, bool isForeground)
   {
       switch (color.kind()) {
       case ZzColor::Kind::Rgb:
           return QColor(color.red(), color.green(), color.blue());
       case ZzColor::Kind::Indexed: {
           const int idx = color.index();
           if (idx >= 0 && idx < 16)
               return kDefaultPalette[idx];
           // 256 色扩展段（16-255）：xterm 标准立方体+灰阶公式
           if (idx >= 16 && idx < 232) {
               const int v = idx - 16;
               const int r = v / 36, g = (v % 36) / 6, b = v % 6;
               auto lv = [](int c) { return c == 0 ? 0 : 55 + c * 40; };
               return QColor(lv(r), lv(g), lv(b));
           }
           if (idx >= 232 && idx < 256) {
               const int gray = 8 + (idx - 232) * 10;
               return QColor(gray, gray, gray);
           }
           return isForeground ? QColor(0xcc, 0xcc, 0xcc)
                               : QColor(0x00, 0x00, 0x00);
       }
       default: // Default
           return isForeground ? QColor(0xcc, 0xcc, 0xcc)
                               : QColor(0x00, 0x00, 0x00);
       }
   }
   ```
   绘制：先 fillRect 背景格（resolveCellColor(cell.background, false)），
   再 setPen(resolveCellColor(cell.foreground, true)) drawText。
   （ZzColor 的 kind()/red()/green()/blue()/index() 访问器名以
   ZzTermCore include/ZzTerm/Cell.h 实际为准，若名字不同以它为准
   并在报告记录。）
5. ZzColor::Kind 枚举值名、ZzCursorState、ZzStitchedLineView 等类型
   均来自 ZzTerm 公共头（include 路径 `<ZzTerm/Terminal.h>` +
   `<ZzTerm/UnwrapView.h>`）。
6. 删 spike 的 main.cpp 宿主逻辑（不在本文件）；setFont 固定字体
   保留（QFontDatabase::systemFont(FixedFont)），新增
   setTerminalFontPointSize 改字号后 updateCellMetrics + 触发
   resize 重算（调 QWidget::updateGeometry + resizeEvent 自然链路，
   即 setFont 后显式调一次 updateCellMetrics() + update()）。

- [ ] **步骤 4：运行测试确认通过**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug -j$(nproc) && ctest --preset linux-gcc-debug -R ZzCoreTerminalWidget --output-on-failure
```

预期：5 用例全绿。

- [ ] **步骤 5：Commit**

```bash
git add src/terminal/ZzCoreTerminalWidget.h src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp tests/CMakeLists.txt
git commit -m "feat(terminal): Z1 ZzCoreTerminalWidget——spike 升格新前端 + 默认调色板配色"
```

### 任务 3：ZzLocalPtyTransport 换 ZzTerm::Pty（ptyqt 退役）

**文件：**
- 修改：`src/transport/ZzLocalPtyTransport.cpp`（整文件重写；.h 不动）
- 测试：修改 `tests/unit/tst_ZzLocalPtyTransport.cpp`（接口不变则微调；先读现状）

- [ ] **步骤 1：读既有测试**，确认它经 ZzTransportInterface 公共接口
  驱动（open/write/resize/close + dataReceived/disconnected）。若
  测试只触公共接口则零改动；若触 ptyqt 私有符号则改为公共接口等价
  断言。运行确认基线绿：
  `ctest --preset linux-gcc-debug -R ZzLocalPtyTransport --output-on-failure`

- [ ] **步骤 2：重写 ZzLocalPtyTransport.cpp**（完整替换内容）：

```cpp
#include "ZzLocalPtyTransport.h"

#include <QtCore/QDir>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QSocketNotifier>
#include <QtCore/QThread>

#include <cstdio>
#include <span>
#include <string>

#include <ZzTerm/Pty.h>

#if defined(Q_OS_WIN)
#include <QtCore/QCoreApplication>
#endif

namespace {

/** @brief 按平台取默认本地 shell（沿用 ptyqt 时代规则）。 */
QString zzDefaultShell()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("c:\\Windows\\system32\\WindowsPowerShell\\v1.0\\powershell.exe");
#else
    const QString fromEnv = QString::fromLocal8Bit(qgetenv("SHELL"));
    return fromEnv.isEmpty() ? QStringLiteral("/bin/sh") : fromEnv;
#endif
}

} // namespace

#if defined(Q_OS_WIN)
/**
 * @brief ConPTY 读取线程：HANDLE 管道无 QSocketNotifier 可用，
 * 阻塞 read 循环，读到数据/EOF 经 QueuedConnection 投递回 GUI 线程。
 */
class ZzPtyReaderThread : public QThread
{
    Q_OBJECT
public:
    ZzPty *pty = nullptr;   ///< 借用，生命周期由 transport 保证
    std::atomic<bool> stopping = false;

    void run() override
    {
        std::byte buf[64 * 1024];
        while (!stopping.load()) {
            const std::ptrdiff_t n = pty->read(std::span<std::byte>(buf, sizeof(buf)));
            if (n > 0) {
                const QByteArray data(reinterpret_cast<const char *>(buf), (qsizetype)n);
                QMetaObject::invokeMethod(qApp, [this, data]() {
                    emit dataReady(data);
                }, Qt::QueuedConnection);
                continue;
            }
            if (n == 0) { // EOF（子进程退出）
                QMetaObject::invokeMethod(qApp, [this]() { emit eofReached(); },
                                          Qt::QueuedConnection);
                return;
            }
            // -1：Windows 侧 ERROR_NO_DATA 等价 EAGAIN——阻塞读不会出现，
            // 防御性让出时间片
            QThread::msleep(1);
        }
    }

signals:
    void dataReady(const QByteArray &data);
    void eofReached();
};
#endif

ZzLocalPtyTransport::ZzLocalPtyTransport(QObject *parent)
    : ZzTransportInterface(parent)
{
}

ZzLocalPtyTransport::~ZzLocalPtyTransport()
{
    close();
}

void ZzLocalPtyTransport::open(const ZzTransportEndpoint &endpoint)
{
    if (state() != State::Disconnected) {
        return;
    }
    setState(State::Connecting);
    m_closing = false;

    const QString shell = endpoint.shellProgram.isEmpty()
        ? zzDefaultShell()
        : endpoint.shellProgram;
    ZzPtyConfig config;
    config.argv = {shell.toStdString()};
    config.cols = endpoint.cols;
    config.rows = endpoint.rows;
    config.rawMode = false; // 交互会话保持 slave echo/opost/isig
    m_pty = ZzPty::spawn(config);
    if (!m_pty) {
        setState(State::Disconnected);
        emit errorOccurred(2001, tr("启动本地 shell 失败（ZzPty spawn）"));
        return;
    }

#if defined(Q_OS_WIN)
    // ConPTY：HANDLE 管道走读取线程
    m_reader = new ZzPtyReaderThread();
    m_reader->pty = m_pty.get();
    connect(m_reader, &ZzPtyReaderThread::dataReady, this,
            [this](const QByteArray &data) { emit dataReceived(data); });
    connect(m_reader, &ZzPtyReaderThread::eofReached, this,
            [this]() { handleShellExit(); });
    m_reader->start();
#else
    // Unix：master fd 非阻塞 + QSocketNotifier 边界触发读循环
    fcntl(m_pty->masterFd(), F_SETFL, O_NONBLOCK);
    m_notifier = new QSocketNotifier(m_pty->masterFd(), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this]() {
        std::byte buf[64 * 1024];
        for (;;) {
            const std::ptrdiff_t n =
                m_pty->read(std::span<std::byte>(buf, sizeof(buf)));
            if (n > 0) {
                emit dataReceived(QByteArray(reinterpret_cast<const char *>(buf),
                                             (qsizetype)n));
                continue;
            }
            if (n == 0) { // EOF（子进程退出归一）
                handleShellExit();
            }
            break; // -1（含 EAGAIN）：本轮读完
        }
    });
#endif

    setState(State::Connected);
}

void ZzLocalPtyTransport::handleShellExit()
{
    if (!m_pty || state() == State::Disconnected) {
        return;
    }
    if (!m_closing) {
        setState(State::Disconnected);
        const auto code = m_pty->tryWait();
        emit disconnected(tr("本地 shell 已退出（退出码 %1）")
                              .arg(code ? *code : -1));
    }
}

void ZzLocalPtyTransport::write(const QByteArray &data)
{
    if (m_pty && state() == State::Connected) {
        m_pty->writeAll(std::as_bytes(std::span<const char>(data.constData(),
                                                            data.size())));
    }
}

void ZzLocalPtyTransport::resize(int cols, int rows)
{
    if (m_pty) {
        m_pty->resize(cols, rows);
    }
}

void ZzLocalPtyTransport::close()
{
    if (!m_pty) {
        setState(State::Disconnected);
        return;
    }
    m_closing = true;
#if defined(Q_OS_WIN)
    if (m_reader) {
        m_reader->stopping.store(true);
        m_pty.reset(); // ClosePseudoConsole 使阻塞读断裂退出
        m_reader->wait(2000);
        delete m_reader;
        m_reader = nullptr;
    } else {
        m_pty.reset();
    }
#else
    delete m_notifier;
    m_notifier = nullptr;
    m_pty.reset(); // 析构 SIGHUP 子进程并回收
#endif
    setState(State::Disconnected);
}
```

`.h` 不动——但成员声明要换：`#include "ptyqt.h"` 相关删除，
`m_pty` 类型改 `std::unique_ptr<ZzPty>`，Unix 加
`QSocketNotifier *m_notifier = nullptr;`，Windows 加
`ZzPtyReaderThread *m_reader = nullptr;`（前向声明），`handleShellExit()`
私有方法声明补上，`m_closing` 保留。includes 加 `<memory>`，
Unix 侧 cpp 顶部加 `#include <fcntl.h>`。实现者按此清单核对 .h 现状
逐条落实（.h 现状很小，允许直接编辑；保持既有注释风格）。
Windows 分支的 moc：ZzPtyReaderThread 在本 cpp 内含 Q_OBJECT，
文件尾需 `#include "ZzLocalPtyTransport.moc"`（AUTOMOC 约定）；
若 Windows CI 报 moc 相关问题，改把该类移入 .h（记录偏差理由）。

- [ ] **步骤 3：测试绿 + 全量回归**

```bash
cmake --build --preset linux-gcc-debug -j$(nproc) && ctest --preset linux-gcc-debug --output-on-failure 2>&1 | tail -3
```

预期：tst_ZzLocalPtyTransport 绿（含真实 spawn 用例），全量不劣于基线。

- [ ] **步骤 4：Commit**

```bash
git add src/transport/ZzLocalPtyTransport.h src/transport/ZzLocalPtyTransport.cpp tests/unit/tst_ZzLocalPtyTransport.cpp
git commit -m "refactor(transport): Z1 本地 PTY 传输换血——ptyqt 退役，ZzTerm::Pty 接管"
```

### 任务 4：ZzTerminalView 换血 + 转码层 + 摘除 qtermwidget（编译原子提交）

**文件：**
- 修改：`src/terminal/ZzTerminalView.h` / `.cpp`
- 修改：`src/tab/ZzTabManager.cpp:59-60,371-372,464-465`
- 修改：`src/CMakeLists.txt`（摘 `ZzQTermWidget::qtermwidget` 链接）、`CMakeLists.txt:30-35`（摘 ZzTermWidget add_subdirectory 段）
- 重写：`tests/unit/tst_ZzTerminalView.cpp`
- 摘除登记：`tests/CMakeLists.txt`（注释 tst_ZzSettingsPage/tst_ZzConnectFlow/tst_ZzSplitContainer）、`tests/perf/CMakeLists.txt`（注释 tst_PerfScrollback）；tst_ZzScrollbackBridge 摘除待任务 5 重写回

- [ ] **步骤 1：ZzTabManager 换接口**

三处 `view->termWidget()->screenColumnsCount()` →
`view->termWidget()->gridCols()`；`screenLinesCount()` → `gridRows()`。
（termWidget() 返回类型变为 ZzCoreTerminalWidget*——见步骤 2。）

- [ ] **步骤 2：ZzTerminalView.h 换血**

- 前向声明 `class QTermWidget;` → `class ZzCoreTerminalWidget;`
- `termWidget()` 返回类型改 `ZzCoreTerminalWidget *`
- `m_term` 类型改 `ZzCoreTerminalWidget *`
- 新增私有成员（转码层）：
  ```cpp
  QStringDecoder m_decoder;              ///< 收方向：会话编码 → UTF-8（ feed 前）
  QStringEncoder m_encoder;              ///< 发方向：UTF-8 → 会话编码（write 前）
  ```
- 头文件注释块更新（职责段：QTermWidget → ZzCoreTerminalWidget +
  ZzTerminal 引擎；加转码层一句）。

- [ ] **步骤 3：ZzTerminalView.cpp 换血**（改动点清单，未列处逐字保留）

1. include：`"qtermwidget.h"` → `"terminal/ZzCoreTerminalWidget.h"`。
2. 构造函数：
   ```cpp
   m_term = new ZzCoreTerminalWidget(this);
   ```
   删 setScrollBarPosition/setMinimumTerminalSize（最小网格约束的
   等价物登记为 Z2 特性 9；Z1 暂缺，注释标明）。
3. 键盘方向 connect 改：
   ```cpp
   connect(m_term, &ZzCoreTerminalWidget::sendData, this,
           [this](const QByteArray &data) {
               if (m_transport) {
                   m_transport->write(m_encoder.encode(QString::fromUtf8(data)));
               }
           });
   ```
4. 尺寸方向 connect 改：
   ```cpp
   connect(m_term, &ZzCoreTerminalWidget::gridResized, this,
           [this](int cols, int rows) {
               m_resizeDebouncer.post(cols, rows);
               emit sizeChanged(cols, rows);
           });
   ```
   （去抖 settled → transport->resize 的 connect 逐字不动。）
5. setTransport 的数据方向改：
   ```cpp
   connect(m_transport, &ZzTransportInterface::dataReceived, this,
           [this](const QByteArray &data) {
               m_term->recvData(m_decoder.decode(data).toUtf8());
           });
   ```
6. applySettings：
   ```cpp
   void ZzTerminalView::applySettings(const ZzAppSettings &settings)
   {
       m_term->setTerminalFontPointSize(settings.fontSize());

       m_encoding = settings.encoding();
       const auto encoding =
           QStringConverter::encodingForName(settings.encoding().toUtf8().constData());
       if (encoding) {
           m_decoder = QStringDecoder(*encoding);
           m_encoder = QStringEncoder(*encoding);
       }
       // 配色方案表与 historyLines 运行时调整属 Z2 收口（Z1 固定默认
       // 调色板 + 构造期 10000 行）；设置值仍读存，切换配色暂不生效。
       (void)settings.colorScheme();
       (void)settings.historyLines();
   }
   ```
   构造尾部 `applySettings(ZzAppSettings::instance())` 保留——它现在
   同时初始化转码器（含默认 UTF-8）。
7. enableScrollback：`new ZzScrollbackBridge(m_term, engine, this)`
   的桥构造签名任务 5 才改——本任务桥构造处暂改为
   `new ZzScrollbackBridge(nullptr, engine, this)`**不行**（桥依赖
   dupDisplayOutput）——裁定：本任务内把桥追加路径改为「widget
   feed 差分」（见步骤 4 的桥改），与落盘换源同 commit，避免半死
   代码。读回注入（setHistoryProvider）删除——Z2 特性 3 重建。
8. `ZZQTERMWIDGET_*` 与 add_subdirectory(third_party/ZzTermWidget)
   段从根 CMakeLists 删除；src/CMakeLists.txt 摘
   `ZzQTermWidget::qtermwidget` 链接行。

- [ ] **步骤 4：ZzScrollbackBridge 落盘换源（同 commit）**

`ZzScrollbackBridge.h/.cpp`：
- 构造签名改 `ZzScrollbackBridge(ZzCoreTerminalWidget *term,
  ZzLogEngine *engine, QObject *parent)`；
- 追加路径改为 feed 差分：
  ```cpp
  // 追加路径（M17 口径）：feed 后 HistoryView 代计数差分——新增的
  // scrollout 定稿行取纯文本追加。比 dupDisplayOutput 显示镜像干净
  //（无重绘噪音）；读回注入留 Z2。
  connect(m_term, &ZzCoreTerminalWidget::contentFed, this, [this]() {
      const auto &hv = m_term->terminal().historyView();
      const std::size_t count = hv.lineCount();
      const std::uint64_t dropped = hv.droppedLineCount();
      // 绝对序号对账：seen 为已追加的绝对行号（含 dropped 偏移）
      std::uint64_t newest = dropped + count;
      while (m_seenLineNo < newest) {
          const std::size_t idx = (std::size_t)(m_seenLineNo - dropped);
          if (idx >= count) // dropped 跳变超过未同步段：跳过缺口
              break;
          const ZzLineView line = hv.lineAt(idx);
          QString text;
          const int n = line.cellCount();
          for (int c = 0; c < n; ++c)
              text += QString::fromStdString(line.cellAt(c).text);
          while (text.endsWith(QLatin1Char(' ')))
              text.chop(1);
          m_engine->appendLine({text, QByteArray()});
          ++m_seenLineNo;
      }
      if (m_seenLineNo < dropped) // 缺口跳齐
          m_seenLineNo = dropped;
  });
  ```
  widget 侧信号 `contentFed()` 已在任务 2 的 ZzCoreTerminalWidget
  中随 recvData 落地（feed 完成后发射），本处直接接线。
  注意 wrapped 折链：scrollout 的历史行是物理行，长命令输出按物理行
  落盘（与 dupDisplayOutput 逐显示行口径一致）。
- readOlderLines/totalLines/simulateDegradationForTest 保留
  （Z2 读回桥复用）；删 setHistoryProvider 接线。
- tst_ZzScrollbackBridge.cpp：本任务从构建摘除（CMake 注释），
  任务 5 重写回。

- [ ] **步骤 5：重写 tst_ZzTerminalView.cpp**（完整替换）：

```cpp
#include <QtTest/QtTest>

#include "ZzMockTransport.h"
#include "settings/ZzAppSettings.h"
#include "terminal/ZzCoreTerminalWidget.h"
#include "terminal/ZzScrollbackBridge.h"
#include "terminal/ZzTerminalView.h"
#include "transport/ZzTransportRegistry.h"

/**
 * @brief Z1 换血后终端视图胶水契约：双向字节流（含转码）、尺寸去抖
 * 转发、设置应用、状态透传、滚动历史降级提示与温层唯一性。
 */
class tst_ZzTerminalView : public QObject
{
    Q_OBJECT
private slots:
    void init() { qRegisterMetaType<ZzTransportInterface::State>(); }

    void bidirectionalByteStream()
    {
        ZzTerminalView view;
        auto *transport = new ZzMockTransport(&view);
        transport->echoEnabled = false;
        view.setTransport(transport);

        ZzTransportEndpoint endpoint;
        transport->open(endpoint);
        QTRY_VERIFY(transport->state() == ZzTransportInterface::State::Connected);

        // 远端 → 终端：feed 后渲染文本可见
        transport->simulateData("hello remote\r\n");
        QCoreApplication::processEvents();
        QCOMPARE(view.termWidget()->visibleLineTextForTest(0),
                 QString("hello remote"));

        // 终端 → 远端：键盘经 sendData → 转码（UTF-8 直通）→ write
        QTest::keyClicks(view.termWidget(), "ls");
        QTest::keyClick(view.termWidget(), Qt::Key_Return);
        QCoreApplication::processEvents();
        QCOMPARE(transport->writtenData, QByteArray("ls\r"));
    }

    void gbkTranscodingRoundTrip()
    {
        const QString path = QDir(QDir::tempPath())
            .filePath(QStringLiteral("zzclawterm-view-gbk.ini"));
        QFile::remove(path);
        ZzAppSettings settings(path);
        settings.setEncoding(QStringLiteral("GBK"));

        ZzTerminalView view;
        view.applySettings(settings);
        QCOMPARE(view.encoding(), QStringLiteral("GBK"));
        auto *transport = new ZzMockTransport(&view);
        transport->echoEnabled = false;
        view.setTransport(transport);
        transport->open(ZzTransportEndpoint{});
        QTRY_VERIFY(transport->state() == ZzTransportInterface::State::Connected);

        // 收：GBK 字节「中」（0xD6 0xD0）→ UTF-8 feed → 渲染为「中」
        transport->simulateData(QByteArray("\xD6\xD0\r\n", 4));
        QCoreApplication::processEvents();
        QVERIFY(view.termWidget()->visibleLineTextForTest(0)
                    .startsWith(QStringLiteral("中")));
        QFile::remove(path);
    }

    void sizeForwardsToTransport()
    {
        ZzTerminalView view;
        auto *transport = new ZzMockTransport(&view);
        view.setTransport(transport);
        QSignalSpy sizeSpy(&view, &ZzTerminalView::sizeChanged);

        emit view.termWidget()->gridResized(100, 40);
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.first().at(0).toInt(), 100);
        QCOMPARE(transport->resizeCallCount, 0); // 去抖窗口内不转发
        QTRY_COMPARE_WITH_TIMEOUT(transport->resizeCallCount, 1, 1000);
        QCOMPARE(transport->lastCols, 100);
        QCOMPARE(transport->lastRows, 40);
    }

    void sizeStormCoalescesToFinal()
    {
        ZzTerminalView view;
        auto *transport = new ZzMockTransport(&view);
        view.setTransport(transport);
        for (int i = 0; i < 10; ++i) {
            emit view.termWidget()->gridResized(100 + i, 40);
            QTest::qWait(20);
        }
        QTRY_COMPARE_WITH_TIMEOUT(transport->resizeCallCount, 1, 1000);
        QCOMPARE(transport->lastCols, 109);
        QCOMPARE(transport->lastRows, 40);
    }

    void statePassthrough()
    {
        ZzTerminalView view;
        auto *transport = new ZzMockTransport(&view);
        view.setTransport(transport);
        QSignalSpy stateSpy(&view, &ZzTerminalView::stateChanged);
        transport->open(ZzTransportEndpoint{});
        QTRY_COMPARE(view.transportState(), ZzTransportInterface::State::Connected);
        QCOMPARE(stateSpy.count(), 2);
        transport->simulateDisconnect(QStringLiteral("对端关闭"));
        QCOMPARE(view.transportState(), ZzTransportInterface::State::Disconnected);
    }

    // 既有回归两例（降级提示 / 温层唯一）逐字保留——其驱动不涉
    // QTermWidget 符号（enableScrollback 链路 + 信号断言）。
    // ……从旧文件搬移 scrollbackDegradationPromptsOnOpen 与
    // scrollbackWarmFilesAreUniquePerTab，逐字不改……
};

QTEST_MAIN(tst_ZzTerminalView)
#include "tst_ZzTerminalView.moc"
```

旧文件另两例（applyGlobalSettings 的 QTermWidget 断言、
scrollbackRestartsWithFreshLineNumbers 的 dupDisplayOutput 驱动）
属旧契约，删除——后者由任务 5 的桥测试以新口径覆盖。

- [ ] **步骤 6：摘除登记**

tests/CMakeLists.txt 注释掉 4 行（tst_ZzSettingsPage /
tst_ZzConnectFlow / tst_ZzSplitContainer / tst_ZzScrollbackBridge），
每行上方加注释：`# Z1 摘除（QTermWidget 直构造）——随特性收口重写回（总体设计 §6）`；
tests/perf/CMakeLists.txt 同样注释 tst_PerfScrollback。

- [ ] **步骤 7：全量构建 + 测试**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug -j$(nproc) && ctest --preset linux-gcc-debug --output-on-failure 2>&1 | tail -5
```

预期：编译无 qtermwidget 残留符号；测试绿（总数 = 基线 - 5 摘除
+ 1 新增 widget 测试）。

- [ ] **步骤 8：Commit**

```bash
git add src/terminal/ZzTerminalView.h src/terminal/ZzTerminalView.cpp src/terminal/ZzScrollbackBridge.h src/terminal/ZzScrollbackBridge.cpp src/terminal/ZzCoreTerminalWidget.h src/terminal/ZzCoreTerminalWidget.cpp src/tab/ZzTabManager.cpp src/CMakeLists.txt CMakeLists.txt tests/CMakeLists.txt tests/perf/CMakeLists.txt tests/unit/tst_ZzTerminalView.cpp
git commit -m "feat(terminal): Z1 ZzTerminalView 换血 ZzTermCore + 转码层 + 落盘换源；qtermwidget 链接摘除"
```

### 任务 5：桥测试重写 + 文档 + 终验交付

**文件：**
- 重写：`tests/unit/tst_ZzScrollbackBridge.cpp`；修改：`tests/CMakeLists.txt`（恢复注册）
- 修改：`README.md` / `BUILDING.md`（子模块清单与构建说明）

- [ ] **步骤 1：重写桥测试**

新契约：构造桥（ZzCoreTerminalWidget + ZzLogEngine）→ 终端
scrollout 行（喂超屏行数制造历史）→ totalLines 增长、
readOlderLines 取回纯文本；降级提示信号保留断言（从旧文件搬
scrollbackRestartsWithFreshLineNumbers 的等价场景：feed 驱动
而非 dupDisplayOutput）。参照旧文件与任务 4 步骤 4 的差分逻辑写
断言；scrollout 造法：widget.terminal().resize(10,3) 后喂多行。

- [ ] **步骤 2：恢复注册 + 全量绿**

tests/CMakeLists.txt 取消 tst_ZzScrollbackBridge 注释，跑全量
ctest 确认绿。

- [ ] **步骤 3：文档**

README.md / BUILDING.md：子模块清单加 ZzTermCore（说明 contour
分支 pin），ZzTermWidget 标注「Z1 起不再参与构建，Z4 删除」；
构建前置（cmake ≥3.26）更新。

- [ ] **步骤 4：终验 + Commit**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug -j$(nproc) && ctest --preset linux-gcc-debug --output-on-failure 2>&1 | tail -3
git add -A && git commit -m "test+docs(z1): 桥测试新口径重写回 + 文档同步"
```

- [ ] **步骤 5：交付用户手测**

汇报：基线对照（摘除/新增账）、三平台 CI 状态（push 后
`gh run list` 盯绿）、手测清单（规格 §10：本地 bash + SSH localhost
会话的显示/键盘/缩拉/不换行/选区；预期内缺失：配色选择不生效、
无 IME/右键菜单/缩放/搜索栏、历史读回不注入、最小网格约束未做）。

## 自检记录

- 规格覆盖：§3 构建→任务 1；§4 前端→任务 2；§5 换血→任务 4；
  §6 ptyqt→任务 3；§7 转码→任务 4 步骤 3-5/6；§8 落盘换源→
  任务 4 步骤 4 + 任务 5；§9 测试→各任务 + 任务 4 步骤 6 摘除；
  §10 验收→任务 5 步骤 5；§11 文档→任务 5 步骤 3。无遗漏。
- 占位符：配色表数值给了确切来源文件与转写指令（机械提取，非
  逻辑缺失）；旧测试两例「逐字搬移」给了确切函数名与来源文件；
  ZzColor 访问器名给了兜底核查指令。其余代码完整。
- 类型一致性：ZzCoreTerminalWidget 接口（recvData/sendData/
  gridResized/gridCols/gridRows/visibleLineTextForTest/
  visibleStitchedTextForTest/cellWidthForTest/cellHeightForTest/
  setUnwrapMode/maxHScrollOffset/contentFed）在任务 2/4/5 间一致；
  ZzPty API（spawn/ZzPtyConfig/masterFd/read/writeAll/resize/
  tryWait）与 ZzTermCore pty/ZzPty.h 一致；ZzResizeDebouncer
  （post/settled/cancel）与 ZzSshCore 头一致。
- 任务边界：任务 1 纯构建（可独立否）；任务 2 纯新增（可独立否）；
  任务 3 传输独立换血（接口不变）；任务 4 编译原子点；任务 5
  测试+文档收口。
