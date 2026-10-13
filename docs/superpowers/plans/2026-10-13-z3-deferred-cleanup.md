# Z3 延后项收口 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 收口 Z2 终审登记的 23 条延后项 + Z2 手测新发现（配色死字段、宽字符续格光标），根治「日志完整性」三条有界缺口，核销「仅记录」条目。

**架构：** 主战场在 ZzClawTerm 前端（ZzCoreTerminalWidget / ZzTerminalView / ZzScrollbackBridge / ZzSshConfigPage / 测试目录）；日志完整性两条需要 ZzTermCore 一个增量 API 小里程碑（M20：`clearCount` + `resizeScreenScrollout`，沿用 M19「虚函数默认回退 + native 覆写」范式，Core commit 5735ff2 为模板），跨仓流程一次走完。

**技术栈：** Qt6 Widgets/QTest（offscreen）、Qt6::Core5Compat、ZzTermCore 静态库（third_party 子模块，contour 分支）、ZzLogEngine。

**账本：** `.superpowers/sdd/2026-10-13-z3-deferred-cleanup/progress.md`（每个任务完成后登记；核销类条目直接在账本标注证据）。

**通用约定：**
- commit 信息：`type(scope): 中文描述`；直接 commit 到 master（ZzClawTerm）/ contour（ZzTermCore 子模块工作区），push 由主代理统一执行。
- 构建：`cmake --build build/linux-gcc-debug -j$(nproc)`；测试：`QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-gcc-debug`，基线 **51/51**，每任务完成后必须保持。
- Core 侧构建/测试：在 `third_party/ZzTermCore` 工作区按该仓惯例（ctest，基线 52/52 本机）。
- 新写测试涉及临时 ini 一律用 `QTemporaryDir`（任务 2 确立的范式），不再新增 `QFile::remove` 首尾仪式。
- 本计划所有行号以 master HEAD=054312d（编码激活后）为准；若漂移按符号名定位。

**延后项 → 任务映射（规格覆盖总表）：**

| 账本条目 | 内容 | 去向 |
|---|---|---|
| :3 | palette 静态缓存线程约束注释 | 任务 3 |
| :4 | "Builtin Tango Dark" 字面量脱钩 | 任务 2 |
| :9 | ini 旧方案名回退 | 已解决（eac1e52），任务 3 核销 |
| :10 | 临时 ini 清理残留 | 任务 2 |
| :11 | 越界 Indexed 色回退 | 已落地（5b14fb7），任务 3 核销 |
| :15/:16/:17 | 编码测试三条 | 任务 5 |
| :21/:22 | IME 同事件 preedit + 清除断言 | 任务 6 |
| :31/:32 | 日志完整性两条 | 任务 9（Core）+ 任务 10（桥） |
| :33 | contour 后端回退洞 | 任务 13 登记（不修，理由见任务 13） |
| :34 | pageStep / WideLead 悬挂 / 选区锚点漂移 | 任务 4 / 任务 8 / 任务 8 |
| :38/:44 | contextMenu accept / hi-res 滚轮 | 任务 7 |
| :39 | 选区鼠标仿真用例 | 任务 11 |
| :43/:53 | 像素字体 / sendKey 上限 | 仅记录确认成立，任务 3 核销 |
| :45 | 隐藏期注释措辞 | 任务 3 |
| :52 | Alt 屏滚轮测试收紧 | 任务 11 |
| :56 | paintUnwrapped 不叠加 preedit | 任务 6 |
| 新发现 | 配色死字段激活 | 任务 1 |
| 新发现 | 宽字符续格光标盖字形 | 任务 12 |
| 手测遗留 | 越顶读回 / 最小网格 / Windows GBK | 任务 13 验收清单 |

**任务依赖：** 任务 10 依赖任务 9（Core pin 前移）；任务 8/11/12 与任务 6/7 都改 `ZzCoreTerminalWidget.cpp` 或其测试——SDD 串行执行天然消解，无额外排序约束。建议顺序：1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 12 → 13（风险升序，跨仓任务放中段，登记收官压轴）。

---

## 任务 1：会话级配色字段激活（热身）

**文件：**
- 修改：`src/dialog/ZzSshConfigPage.cpp`（:199-206 启用、:425-427 删 tooltip）
- 修改：`src/terminal/ZzTerminalView.h` / `src/terminal/ZzTerminalView.cpp`（setter + applySettings 分流）
- 修改：`src/tab/ZzTabManager.cpp:145`（createPane 接通）
- 测试：`tests/unit/tst_ZzTerminalView.cpp`、`tests/unit/tst_ZzTabManager.cpp`、`tests/unit/tst_ZzSshConfigPage.cpp`

**背景：** 与 054312d（会话级编码）完全同构。配色 combo 数据层早已打通（setProfile/applyTo/JSON 读写都在），只差 UI 启用 + 消费层接通。`ZzAppShell.cpp:410-418` 的全局刷新会对所有 view 调 `applySettings`，会话级值必须存在 view 内并在 applySettings 里分流，否则被冲刷。底层 `ZzCoreTerminalWidget::setColorScheme`（未知名回退默认方案）已就绪。

- [ ] **步骤 1：编写失败的测试（3 条）**

`tests/unit/tst_ZzTerminalView.cpp` 追加（沿用 sessionEncodingOverridesGlobal 的 QTemporaryDir 范式——若任务 2 尚未执行，本任务新代码直接用 QTemporaryDir）：

```cpp
/**
 * @brief 会话级配色覆盖全局值，且不被后续 applySettings 冲掉；
 *        置回空串后跟随全局（ZzTabManager 开会话时按 profile.colorSchemeName 接通）。
 */
void sessionColorSchemeOverridesGlobal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ZzAppSettings settings(dir.filePath(QStringLiteral("session-scheme.ini")));

    ZzTerminalView view;
    view.setSessionColorScheme(QStringLiteral("Dracula"));
    QCOMPARE(view.termWidget()->colorSchemeName(), QStringLiteral("Dracula"));

    settings.setColorScheme(QStringLiteral("Solarized Light"));
    view.applySettings(settings); // 全局变更不覆盖会话级配色
    QCOMPARE(view.termWidget()->colorSchemeName(), QStringLiteral("Dracula"));

    view.setSessionColorScheme(QString()); // 空串 = 跟随全局
    QCOMPARE(view.termWidget()->colorSchemeName(),
             ZzAppSettings::instance().colorScheme());
}
```

`tests/unit/tst_ZzTabManager.cpp` 追加（紧跟 sessionEncodingFlowsToView）：

```cpp
/** @brief profile.colorSchemeName 接通到会话视图：非空覆盖全局，空串跟随全局。 */
void sessionColorSchemeFlowsToView()
{
    ZzTabManager tabs;
    ZzSessionProfile dark = makeProfile(QStringLiteral("暗色机"));
    dark.colorSchemeName = QStringLiteral("Dracula");
    tabs.openSession(dark);
    QCOMPARE(tabs.viewAt(0)->termWidget()->colorSchemeName(),
             QStringLiteral("Dracula"));

    ZzSessionProfile follow = makeProfile(QStringLiteral("跟随机"));
    follow.colorSchemeName.clear(); // 空串 = 跟随全局设置
    tabs.openSession(follow);
    QCOMPARE(tabs.viewAt(1)->termWidget()->colorSchemeName(),
             ZzAppSettings::instance().colorScheme());
}
```

`tests/unit/tst_ZzSshConfigPage.cpp`：用例 `encodingEnabledColorSchemeDisabled`（:159-167 一带）改名并反转断言：

```cpp
/** @brief 编码与配色 combo 均已接通会话创建（可用，无死字段 tooltip）。 */
void encodingAndColorSchemeEnabled()
{
    ZzSshConfigPage page;
    auto *encoding = page.findChild<QComboBox *>(QStringLiteral("encodingCombo"));
    auto *scheme = page.findChild<QComboBox *>(QStringLiteral("colorSchemeCombo"));
    QVERIFY(encoding && scheme);
    QVERIFY(encoding->isEnabled());
    QVERIFY(scheme->isEnabled());
    QVERIFY(scheme->toolTip().isEmpty());
}
```

- [ ] **步骤 2：运行验证失败**

运行：`QT_QPA_PLATFORM=offscreen ./build/linux-gcc-debug/tests/tst_ZzTerminalView sessionColorSchemeOverridesGlobal`
预期：FAIL（`setSessionColorScheme` 未定义即编译失败，属预期红）

- [ ] **步骤 3：实现（四处）**

`src/terminal/ZzTerminalView.h`（public 区 setSessionEncoding 声明之后 / private 成员区 m_sessionEncoding 之后）：

```cpp
/** @brief 设置会话级配色（空串=跟随全局设置，与 profile 空串口径一致），
 *        立即切换；后续 applySettings 不覆盖非空会话级配色。 */
void setSessionColorScheme(const QString &name);
```

```cpp
QString m_sessionColorScheme;        ///< 会话级配色，空串=跟随全局（ZzTabManager 开会话时设置）
```

`src/terminal/ZzTerminalView.cpp`——applySettings 内配色行（:231 一带 `m_term->setColorScheme(settings.colorScheme());`）改分流，并新增 setter（紧跟 setSessionEncoding 实现之后）：

```cpp
// 会话级配色非空时优先；空串跟随全局，全局变更经本函数生效
m_term->setColorScheme(m_sessionColorScheme.isEmpty()
                           ? settings.colorScheme()
                           : m_sessionColorScheme);
```

```cpp
void ZzTerminalView::setSessionColorScheme(const QString &name)
{
    m_sessionColorScheme = name;
    m_term->setColorScheme(name.isEmpty()
                               ? ZzAppSettings::instance().colorScheme()
                               : name);
}
```

applySettings 的文档注释同步更新为「会话级覆盖优先，空串跟随全局」口径。

`src/tab/ZzTabManager.cpp:145`（setSessionEncoding 行后加一行）：

```cpp
// 会话级配色接通：同编码口径
view->setSessionColorScheme(profile.colorSchemeName);
```

`src/dialog/ZzSshConfigPage.cpp`——删除 `m_colorSchemeCombo->setEnabled(false);`，构造注释更新为「首项『跟随全局』（itemData 空串，空串=跟随全局设置）」；删除 retranslateUi 里 :425-427 的「暂未生效」tooltip 整段。

- [ ] **步骤 4：运行验证通过 + 全量回归**

```bash
cmake --build build/linux-gcc-debug -j$(nproc)
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-gcc-debug
```

预期：三条新/改用例 PASS，全量 51+N 全过。

- [ ] **步骤 5：Commit**

```bash
git add src/dialog/ZzSshConfigPage.cpp src/terminal/ZzTerminalView.h src/terminal/ZzTerminalView.cpp src/tab/ZzTabManager.cpp tests/unit/tst_ZzTerminalView.cpp tests/unit/tst_ZzTabManager.cpp tests/unit/tst_ZzSshConfigPage.cpp
git commit -m "feat(session): 启用并接通会话级配色字段（空串跟随全局，镜像编码 054312d 范式）"
```

- [ ] **步骤 6：用户手测确认**

SSH 配置页选非默认配色开会话即时生效；「跟随全局」回退正确；全局设置切换配色时已设会话级配色的标签页不被冲刷。

---

## 任务 2：测试卫生清理（符号化 + QTemporaryDir）

**文件：**
- 修改：`tests/unit/tst_ZzTerminalPalette.cpp:27`
- 修改：`tests/unit/tst_ZzCoreTerminalWidget.cpp:138`
- 修改：`tests/unit/tst_ZzTerminalView.cpp`（gbkTranscodingRoundTrip / sessionEncodingOverridesGlobal / applySettingsPropagatesColorScheme 三用例）

**背景：** 两处 "Builtin Tango Dark" 硬编码字面量与 `ZzTerminalPalette::defaultSchemeName()` 脱钩——默认方案改名时测试静默走「未知名回退默认」路径名存实亡。三处临时 ini 的 `QFile::remove(path)` 在 QCOMPARE 之后，断言失败时残留（账本行号 :723 已漂移，按用例名定位）。

- [ ] **步骤 1：字面量符号化（两处）**

```cpp
// tst_ZzTerminalPalette.cpp:27
const ZzTerminalPalette::Scheme s =
    ZzTerminalPalette::scheme(ZzTerminalPalette::defaultSchemeName());

// tst_ZzCoreTerminalWidget.cpp:138
widget.setColorScheme(ZzTerminalPalette::defaultSchemeName());
```

（两文件均已 `#include "terminal/ZzTerminalPalette.h"`，零新增依赖。）

- [ ] **步骤 2：QTemporaryDir 化（三处，同构）**

以 `gbkTranscodingRoundTrip` 为例（tst_ZzTerminalView.cpp），其余两例同式：

```cpp
void gbkTranscodingRoundTrip()
{
    QTemporaryDir dir; // 断言失败栈展开亦清理
    QVERIFY(dir.isValid());
    ZzAppSettings settings(dir.filePath(QStringLiteral("gbk.ini")));
    settings.setEncoding(QStringLiteral("GBK"));
    // ……中段不变……删去尾部 QFile::remove(path);
}
```

要点：删首行 `QFile::remove(path)` 与尾行 `QFile::remove(path)`；path 三行替换为 `QTemporaryDir dir; QVERIFY(dir.isValid());` + `dir.filePath(...)`。`<QtTest/QtTest>` 已含 QTemporaryDir。

- [ ] **步骤 3：全量回归 + Commit**

```bash
cmake --build build/linux-gcc-debug -j$(nproc) && QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-gcc-debug
git add tests/unit/tst_ZzTerminalPalette.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp tests/unit/tst_ZzTerminalView.cpp
git commit -m "test: 测试卫生——默认方案名字面量符号化 + 临时 ini 改 QTemporaryDir"
```

预期：全绿，无用例数变化（纯重构）。

---

## 任务 3：注释与口径核销（零风险）

**文件：**
- 修改：`src/terminal/ZzTerminalPalette.cpp:95-101`（loadScheme 缓存注释）
- 修改：`src/terminal/ZzCoreTerminalWidget.cpp:178-180` 与 `:543-545`（隐藏期注释措辞）
- 修改：账本 `.superpowers/sdd/2026-10-13-z3-deferred-cleanup/progress.md`（核销登记）

- [ ] **步骤 1：palette 线程约束注释**

替换 loadScheme 函数头部注释块为：

```cpp
/** @brief 方案缓存（字段级回退：缺失字段取默认方案对应值）。 */
[[nodiscard]] ZzTerminalPalette::Scheme loadScheme(const QString &name)
{
    // 线程约束：本缓存无锁，全部入口（scheme() 及渲染路径 colorForIndex）
    // 仅在 GUI 线程调用——消费方是 paintEvent 与设置页，Qt 约定均在 GUI
    // 线程。若日后引入工作线程取色，需加 QMutex 或改线程本地缓存。
    static QHash<QString, ZzTerminalPalette::Scheme> cache;
```

- [ ] **步骤 2：隐藏期注释措辞（两处）**

`:178-180`（setTerminalFontPointSize 隐藏守卫）替换为：

```cpp
    // 隐藏期不重算：从未 show 的控件 size() 是 Qt 默认占位尺寸（100x30），
    // show 过再隐藏的虽保留最后真实尺寸，但隐藏期尺寸可能再变且
    // resizeEvent 只在可见后投递——统一推迟到 show 后的
    // resizeEvent/showEvent 按新 cell 尺寸一次算准（幂等）。
```

`:543-545`（showEvent）把「（size() 是占位尺寸）」改为「（隐藏期不重算是既定口径）」。

- [ ] **步骤 3：账本核销四条**

在 Z3 账本登记核销结论（含证据指针）：
1. 账本 :9 ini 旧方案名回退——已解决（Z2 收官 eac1e52）；
2. 账本 :11 越界 Indexed 色回退——已落地（5b14fb7；契约 `ZzTerminalPalette.h:33-34`、实现 `ZzTerminalPalette.cpp:176`、测试 `tst_ZzTerminalPalette.cpp:60-61` 三者一致）；
3. 账本 :43 像素字体 pointSize()==-1——确认不可达（字库不可选：构造固定 `QFontDatabase::systemFont(FixedFont)`，设置页只有字号 spin），不加防御代码（会引入死代码），保留记录；
4. 账本 :53 sendKey 次数无上限——确认有界（delta 源自真实设备，单事件量级有度；任务 7 累余数落地后量级不变），仅记录。

- [ ] **步骤 4：全量回归 + Commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-gcc-debug
git add src/terminal/ZzTerminalPalette.cpp src/terminal/ZzCoreTerminalWidget.cpp .superpowers/sdd/2026-10-13-z3-deferred-cleanup/progress.md
git commit -m "docs(terminal): 线程约束与隐藏期注释措辞精度 + Z3 账本核销四条"
```

---

## 任务 4：滚动条 pageStep 接线

**文件：**
- 修改：`src/terminal/ZzTerminalView.cpp:61-111`（滚动条接线块）
- 测试：`tests/unit/tst_ZzTerminalView.cpp`

**背景：** 全仓无 `setPageStep`，QScrollBar 默认 pageStep=10——滑块长度比例失真（历史 10000 行时滑块按 10 行视口算）、点击滑槽只滚 10 行而非一屏。值域模型 `maximum = maxScrollOffset() = 文档长 − 视口` 恰是 Qt「maximum 已扣除 pageStep」口径，无冲突。fork 旧实现 `TerminalDisplay.cpp:3299` 有同款 `_scrollBar->setPageStep(_lines)` 作 parity 佐证。

- [ ] **步骤 1：编写失败的测试**

```cpp
void scrollbarPageStepTracksViewportRows()
{
    ZzTerminalView view;
    const auto bars = view.findChildren<QScrollBar *>();
    QScrollBar *vbar = nullptr, *hbar = nullptr;
    for (QScrollBar *bar : bars) {
        if (bar->orientation() == Qt::Vertical)   vbar = bar;
        if (bar->orientation() == Qt::Horizontal) hbar = bar;
    }
    QVERIFY(vbar && hbar);
    QCOMPARE(vbar->pageStep(), view.termWidget()->gridRows());
    QCOMPARE(hbar->pageStep(), view.termWidget()->gridCols());
    emit view.termWidget()->gridResized(132, 43);
    QCOMPARE(vbar->pageStep(), 43);
    QCOMPARE(hbar->pageStep(), 132);
}
```

- [ ] **步骤 2：运行验证失败**（FAIL：pageStep 为默认 10）

- [ ] **步骤 3：实现**

`ZzTerminalView.cpp` 滚动条接线块后、既有 `gridResized → m_resizeDebouncer` connect（:111）旁边：

```cpp
    // pageStep = 视口行/列数：滑块比例与滑槽点击步进随网格（构造快照 +
    // gridResized 跟随；值域 maximum=scrollOffset 上限即「文档长−视口」，
    // 与 Qt pageStep 口径一致）。对齐 fork TerminalDisplay::setScroll 口径。
    m_vbar->setPageStep(m_term->gridRows());
    m_hbar->setPageStep(m_term->gridCols());
    connect(m_term, &ZzCoreTerminalWidget::gridResized, this,
            [this](int cols, int rows) {
                m_vbar->setPageStep(rows);
                m_hbar->setPageStep(cols);
            });
```

- [ ] **步骤 4：运行验证通过 + 全量回归 + Commit**

```bash
git add src/terminal/ZzTerminalView.cpp tests/unit/tst_ZzTerminalView.cpp
git commit -m "fix(terminal): 滚动条 pageStep 跟随视口行列数（滑块比例与滑槽步进修正）"
```

---

## 任务 5：编码测试补强（GBK 发方向 + 无效编码回退）

**文件：**
- 测试：`tests/unit/tst_ZzTerminalView.cpp`（新增两用例，实现零改动）

**背景：** 054312d 的两级查找（QStringDecoder 按名 → Core5Compat QTextCodec 兜底）+ UTF-8 回退已具备被测行为。缺口：① 现有 `gbkTranscodingRoundTrip` 只测收方向，发方向编码器若静默保持 UTF-8 默认态测不出来（远端乱码）；② 无效编码名回退 UTF-8 是历史黑屏 bug 的修复，无回归钉。注入手法：直接 `emit view.termWidget()->sendData(...)`（同文件 :78 emit gridResized 有先例；`QTest::keyClicks` 对 CJK 无效），字节经 `ZzMockTransport::writtenData` 观测。

- [ ] **步骤 1：编写测试（两条）**

```cpp
/**
 * @brief GBK 发送方向字节级断言：CJK 输入经会话编码转码后
 *        必须产出 GBK 字节序列（「中」= 0xD6 0xD0），而非 UTF-8 直通。
 */
void gbkSendDirectionProducesGbkBytes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ZzAppSettings settings(dir.filePath(QStringLiteral("gbk-send.ini")));
    settings.setEncoding(QStringLiteral("GBK"));

    ZzTerminalView view;
    view.applySettings(settings);
    auto *transport = new ZzMockTransport(&view);
    transport->echoEnabled = false;
    view.setTransport(transport);
    transport->open(ZzTransportEndpoint{});
    QTRY_VERIFY(transport->state() == ZzTransportInterface::State::Connected);

    // 键盘输入方向：UTF-8「中」→ GBK 0xD6 0xD0（ASCII 两编码同形，
    // 无法区分编码器是否生效，必须用 CJK 断言）
    emit view.termWidget()->sendData(QStringLiteral("中").toUtf8());
    QCOMPARE(transport->writtenData, QByteArray("\xD6\xD0", 2));
}
```

```cpp
/**
 * @brief 回归：无效编码名（拼写错误等）回退 UTF-8，收发双向按 UTF-8 语义
 *        工作。曾因保持默认态 decoder 静默返空串导致黑屏。
 */
void invalidEncodingFallsBackToUtf8()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ZzAppSettings settings(dir.filePath(QStringLiteral("bad-encoding.ini")));
    settings.setEncoding(QStringLiteral("NO-SUCH-CODEC"));

    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression(QStringLiteral("无法识别的编码")));
    ZzTerminalView view;
    view.applySettings(settings);
    auto *transport = new ZzMockTransport(&view);
    transport->echoEnabled = false;
    view.setTransport(transport);
    transport->open(ZzTransportEndpoint{});
    QTRY_VERIFY(transport->state() == ZzTransportInterface::State::Connected);

    // 发：UTF-8 直通（「中」= E4 B8 AD）
    emit view.termWidget()->sendData(QStringLiteral("中").toUtf8());
    QCOMPARE(transport->writtenData, QStringLiteral("中").toUtf8());

    // 收：UTF-8 字节正常渲染，不返空串（黑屏回归的直接探针）
    transport->simulateData(QStringLiteral("中\r\n").toUtf8());
    QCoreApplication::processEvents();
    QVERIFY(view.termWidget()->visibleLineTextForTest(0)
                .startsWith(QStringLiteral("中")));
}
```

（需补 `#include <QtCore/QRegularExpression>`；`ignoreMessage` 须在 applySettings 之前；emit 同步触发 lambda，无需 processEvents。）

- [ ] **步骤 2：运行验证通过**

这两条是对既有行为的钉板，预期直接 PASS（若 FAIL 说明两级查找/回退被回归，属真发现）。

- [ ] **步骤 3：全量回归 + Commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-gcc-debug
git add tests/unit/tst_ZzTerminalView.cpp
git commit -m "test(terminal): GBK 发送方向字节级断言 + 无效编码名回退 UTF-8 回归钉"
```

注：账本 :17（encoding() 断言仅回显字符串）由第一条用例的行为断言覆盖——`sessionEncodingOverridesGlobal` 里的 `encoding()` 断言保留（那里测路由逻辑，回显字符串恰是正确断言对象）。

---

## 任务 6：IME 收口（同事件 preedit + 清除断言 + unwrap preedit 绘制）

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.cpp`（inputMethodEvent :502-512；paintUnwrapped 光标块 :325-341）
- 测试：`tests/unit/tst_ZzCoreTerminalWidget.cpp`

**背景：** a) QInputMethodEvent 的 commit 与 preedit 不互斥（连续上屏时 commit "中文" 同事件携带下一拍 preedit "zh"），现 if/else 互斥分支吞掉同事件 preedit，视觉丢一拍；b) commit 清除 preedit 路径无断言；c) paintUnwrapped 无 preedit 绘制（Z2 YAGNI 登记）。a/b 是同行为正反两面，必须同任务。

- [ ] **步骤 1：编写失败的测试（两条）**

```cpp
void imeCommitWithPreeditSameEventKeepsPreedit()
{
    ZzCoreTerminalWidget widget;
    QSignalSpy spy(&widget, &ZzCoreTerminalWidget::sendData);
    // 同一事件同时携带 commit 与下一拍 preedit（连续上屏场景）
    QInputMethodEvent ev(QStringLiteral("zh"),
                         QList<QInputMethodEvent::Attribute>{});
    ev.setCommitString(QStringLiteral("中"));
    QApplication::sendEvent(&widget, &ev);
    QVERIFY(spy.count() >= 1);
    QByteArray all;
    for (const auto &call : spy)
        all += call.at(0).toByteArray();
    QCOMPARE(all, QStringLiteral("中").toUtf8());                 // commit 进会话
    QCOMPARE(widget.preeditTextForTest(), QStringLiteral("zh")); // preedit 不丢
}

void imeCommitClearsPreedit()
{
    ZzCoreTerminalWidget widget;
    QSignalSpy spy(&widget, &ZzCoreTerminalWidget::sendData);
    QInputMethodEvent preedit(QStringLiteral("zhong"),
                              QList<QInputMethodEvent::Attribute>{});
    QApplication::sendEvent(&widget, &preedit);
    QCOMPARE(widget.preeditTextForTest(), QStringLiteral("zhong"));
    // 纯 commit 事件（无 preedit）后 preedit 必须清空
    QInputMethodEvent commit(QString(), QList<QInputMethodEvent::Attribute>{});
    commit.setCommitString(QStringLiteral("中文"));
    QApplication::sendEvent(&widget, &commit);
    QVERIFY(spy.count() >= 1);
    QCOMPARE(widget.preeditTextForTest(), QString());
}
```

- [ ] **步骤 2：运行验证失败**

预期：`imeCommitWithPreeditSameEventKeepsPreedit` FAIL（末行 QCOMPARE 实际为空串）；`imeCommitClearsPreedit` PASS（补钉用例）。

- [ ] **步骤 3：实现 inputMethodEvent 修复**

```cpp
void ZzCoreTerminalWidget::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty())
        term_.sendText(event->commitString().toUtf8().toStdString());
    // commit 与 preedit 可同事件携带（连续上屏时 preedit 已是下一拍），
    // 一律以事件值为准，空串即清除。
    preedit_ = event->preeditString();
    update();
    event->accept();
}
```

（原 commit 分支的 `preedit_.clear()` 被「赋值为事件 preedit」自然涵盖：纯 commit 事件 preeditString() 必为空，Qt 契约三平台均满足。）

- [ ] **步骤 4：运行验证通过**

- [ ] **步骤 5：paintUnwrapped 叠加 preedit + 状态用例**

`paintUnwrapped` 光标块（:325-341）内 `if (row >= 0 && ...)` 守卫里、光标 fillRect 之后追加（坐标换算复用光标块现成的拼接坐标链路）：

```cpp
            // IME preedit：与 paintWrapped 同样式（下划线），叠加于光标格起。
            if (!preedit_.isEmpty()) {
                QFont f = painter.font();
                f.setUnderline(true); // preedit 惯例样式：下划线
                painter.setFont(f);
                painter.setPen(scheme_.foreground);
                painter.setClipRect(rect());
                painter.drawText(col * cellWidth_,
                                 (int)row * cellHeight_ + cellAscent_,
                                 preedit_);
                painter.setClipping(false);
            }
```

语义裁决（写进 commit message）：光标因横滚被裁出右窗时 preedit 随光标不画——与光标行为一致，属合理口径。

测试（状态级；paint 无像素基建，grab 冒烟可选）：

```cpp
void imePreeditTrackedInUnwrapMode()
{
    ZzCoreTerminalWidget widget;
    widget.recvData(QByteArray("abcdef\r\n"));
    widget.setUnwrapMode(true);
    QSignalSpy spy(&widget, &ZzCoreTerminalWidget::sendData);
    QInputMethodEvent preedit(QStringLiteral("zhong"),
                              QList<QInputMethodEvent::Attribute>{});
    QApplication::sendEvent(&widget, &preedit);
    QCOMPARE(widget.preeditTextForTest(), QStringLiteral("zhong"));
    QCOMPARE(spy.count(), 0);
}
```

- [ ] **步骤 6：全量回归 + Commit**

```bash
git add src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "fix(terminal): IME 同事件 commit+preedit 不再丢拍，unwrap 模式叠加 preedit 绘制"
```

---

## 任务 7：事件健壮性两件套（contextMenu accept + hi-res 滚轮累余数）

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.cpp`（contextMenuEvent 尾部 :850 一带；wheelEvent :654-696）、`src/terminal/ZzCoreTerminalWidget.h`（:170 附近加成员）
- 测试：`tests/unit/tst_ZzCoreTerminalWidget.cpp`

**背景：** c) `contextMenuEvent` 未 accept，向父控件传播（当前父链无人处理故无害，父级哪天加右键处理就双响应）；d) hi-res 滚轮（触控板）单事件 delta ±15~60 不足一档 120，`angleDelta().y()/120` 整除得 0 被吞，滚动偏钝。

- [ ] **步骤 1：编写失败的测试（两条）**

```cpp
void contextMenuEventAccepted()
{
    // accept 语义回归：菜单弹出即关（exec 返回 nullptr），事件须已被消费。
    ZzCoreTerminalWidget widget;
    QTimer::singleShot(0, [] {
        if (QWidget *popup = QApplication::activePopupWidget())
            popup->close(); // 嵌套事件循环内关菜单，防 exec 阻塞
    });
    QContextMenuEvent e(QContextMenuEvent::Mouse, QPoint(5, 5),
                        widget.mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(&widget, &e);
    QVERIFY(e.isAccepted());
}

void hiResWheelAccumulatesRemainder()
{
    // hi-res 小 delta 累余数：两次 +60（各不足一档）须合成一档上滚 3 行；
    // 修复前 60/120==0 被整除截断逐次吞掉。
    ZzCoreTerminalWidget widget;
    widget.show(); // 隐藏 widget 的 resize() 不投递 QResizeEvent
    for (int i = 0; i < 60; ++i)
        widget.recvData(QByteArray("line\r\n"));
    QVERIFY(widget.maxScrollOffset() > 0);
    const auto wheelUp60 = [](QWidget *w) {
        QWheelEvent e(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0),
                      QPoint(0, 60), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
        QApplication::sendEvent(w, &e);
    };
    wheelUp60(&widget);
    QCOMPARE(widget.scrollOffset(), 0); // 不足一档暂不滚
    wheelUp60(&widget);
    QCOMPARE(widget.scrollOffset(), 3); // 凑满一档：每档 3 行
}
```

（文件头补 `#include <QTimer>`。）

- [ ] **步骤 2：运行验证失败**（两用例均 FAIL）

- [ ] **步骤 3：实现**

contextMenuEvent 尾部（clear 分支 `}` 之后、函数结束前）：

```cpp
    event->accept(); // 菜单已处置（含取消）：阻断向父控件传播
```

wheelEvent——`ZzCoreTerminalWidget.h` 成员区加：

```cpp
int wheelRemainder_ = 0;           ///< hi-res 滚轮 delta 余数（凑满 120 成一档）
```

`wheelEvent` 开头统一累余数，删除三处（:657/:670/:685）各自的 `const int steps = event->angleDelta().y() / 120;`：

```cpp
void ZzCoreTerminalWidget::wheelEvent(QWheelEvent *event)
{
    // hi-res 滚轮（触控板/精密滚轮）单事件 delta 常不足一档 120：
    // 累计余数、凑满成档，整除截断不再吞掉小 delta。缩放/alt/普通
    // 三分支共用一个累加器（切换修饰键不清零，与 Konsole 同口径）。
    wheelRemainder_ += event->angleDelta().y();
    const int steps = wheelRemainder_ / 120;
    wheelRemainder_ -= steps * 120;

    if (event->modifiers() & Qt::ControlModifier) {
        // ……后续分支体不变……
```

- [ ] **步骤 4：运行验证通过 + 全量回归 + Commit（两个）**

```bash
git add src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "fix(terminal): contextMenuEvent 补 event->accept() 阻断父级传播"
git add src/terminal/ZzCoreTerminalWidget.h src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "fix(terminal): hi-res 滚轮 delta 累余数凑档，小步距不再被整除截断"
```

（若两改动工作区纠缠不便拆 commit，可单 commit 两fix 并在 message 列两条。）

---

## 任务 8：读回注入健全性（WideLead 悬挂守卫 + 拖拽锚点正移）

**文件：**
- 修改：`src/terminal/ZzScrollbackBridge.cpp:127-148`（injectOlderBlock 循环体 + notifyHistoryPrepended 调用点）
- 修改：`src/terminal/ZzCoreTerminalWidget.h:125` / `.cpp:698-703`（notifyHistoryPrepended 加参 + 锚点正移）
- 测试：`tests/unit/tst_ZzScrollbackBridge.cpp`、`tests/unit/tst_ZzCoreTerminalWidget.cpp`

**背景：** b) 注入循环在宽字符恰落行末（放置前 col==cols-1）时不写续格，留下悬挂 WideLead——reflow 把它当折行链首（`Reflow.cpp:67-71` flushRow(true)），与无关行拼链污染 UnwrapView/选区/搜索；Core 自身 putChar 同场景换行留空（`ZzNativeBackend.cpp:418-432`），悬挂只能由注入产生。c) 拖拽选区进行中发生越顶注入时，widget 私有锚点副本 `selAnchor_` 不被 Core 的 onLinesPrepended 正移，下次 mouseMove 全量重放把选区盖回漂移位置（窄路径：按住左键滚轮越顶）。顺序：先 b 后 c（c 改函数尾部签名，b 先落地让 diff 干净）。

- [ ] **步骤 1：编写失败的测试（b）**

`tests/unit/tst_ZzScrollbackBridge.cpp` 追加（注入需归还额度即 dropped>0，沿用 scrollTopInjectsOlderLines 的填容量范式）：

```cpp
/**
 * @brief 回归：读回行宽字符恰落行末边界时丢弃（不注入悬挂 WideLead）。
 *        10 列：「123456789中」宽字符起于末列 → 截断为「123456789」且
 *        末列非 WideLead；悬挂会在 reflow 时被当折行链首污染逻辑行结构。
 */
void injectDropsWideCharAtLineEndBoundary()
{
    ZzCoreTerminalWidget term;
    term.terminal().resize(10, 3);
    ZzLogEngine::Config config;
    config.hotCapacity = 20000;
    ZzLogEngine engine(config);
    QVERIFY(engine.open());
    ZzScrollbackBridge bridge(&term, &engine);

    term.recvData(QStringLiteral("123456789中\r\n").toUtf8()); // Core 折 2 行落盘
    for (int block = 0; block < 101; ++block) {
        QByteArray chunk;
        for (int i = 0; i < 100; ++i)
            chunk += QStringLiteral("L%1\r\n").arg(block * 100 + i).toUtf8();
        term.recvData(chunk);
    }
    const auto &hv = term.terminal().historyView();
    QVERIFY(hv.droppedLineCount() > 0);

    bridge.injectOlderBlock(200);
    const ZzLineView line0 = hv.lineAt(0);
    QCOMPARE(line0.cellAt(9).width, ZzCellWidth::Narrow); // 末列不得是 WideLead
    QCOMPARE(historyLineText(hv, 0), QStringLiteral("123456789"));
}
```

（实现时按引擎实际行号窗口微调注入目标行定位；断言核心两条：末列 width 非 WideLead、文本截到「123456789」。可补「12345678中」恰好占满 8-9 两列配对完整的对照断言。）

- [ ] **步骤 2：运行验证失败**（FAIL：末列为 WideLead / 文本含「中」前半）

- [ ] **步骤 3：实现 b（注入守卫）**

injectOlderBlock 注入循环（:127-141）改为：

```cpp
        for (const char32_t cp : text.toUcs4()) { // UCS-4 迭代：BMP 外按整码位
            if (col >= cols)
                break; // 读回行超宽截断（纯文本口径，丢样式为既有口径）
            // 宽度取引擎同一张 EAW 表（zzCellWidthOf），与渲染口径一致
            const bool wide = zzCellWidthOf(cp) == 2;
            if (wide && col + 2 > cols)
                break; // 宽字符落行末边界无续格空间：丢弃——悬挂 WideLead
                       // 会在 reflow 时被当折行链首（flushRow(true)），
                       // 污染逻辑行结构；Core putChar 同场景换行留空
            ZzCell cell;
            cell.setCodePoint(cp);
            cell.setWidth(wide ? ZzCellWidth::WideLead : ZzCellWidth::Narrow);
            line.setCell(col++, cell);
            if (wide) { // 宽字符占两格，续格占位（边界已在上方排除）
                ZzCell cont;
                cont.setWidth(ZzCellWidth::WideContinuation);
                line.setCell(col++, cont);
            }
        }
```

- [ ] **步骤 4：运行验证通过（b）**

- [ ] **步骤 5：编写失败的测试（c）**

`tests/unit/tst_ZzCoreTerminalWidget.cpp` 追加：

```cpp
void selectionAnchorShiftsOnHistoryPrepend()
{
    // 回归：拖拽选区进行中发生读回注入，锚点副本须随内容顺移，
    // 否则下次 mouseMove 重放旧锚点把选区盖回漂移位置。
    ZzCoreTerminalWidget widget;
    widget.terminal().resize(10, 4);
    widget.show();
    widget.resize(10 * widget.cellWidthForTest(),
                  4 * widget.cellHeightForTest());
    widget.recvData(QByteArray("aaaa\r\nbbbb\r\ncccc\r\ndddd\r\n"));

    const int cw = widget.cellWidthForTest();
    const int ch = widget.cellHeightForTest();
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(cw / 2, ch / 2)); // 锚在第 0 行
    widget.notifyHistoryPrepended(5);          // 注入 5 行（等价桥回调）
    QTest::mouseMove(&widget, QPoint(3 * cw, 2 * ch + ch / 2));
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier,
                        QPoint(3 * cw, 2 * ch + ch / 2));

    ZzLogicalPos start, end;
    QVERIFY(widget.terminal().selectionRange(start, end));
    QCOMPARE(start.line, std::int64_t(5)); // 锚点正移：内容行 0 → 5
}
```

- [ ] **步骤 6：运行验证失败**（编译失败：notifyHistoryPrepended 无参——预期红）

- [ ] **步骤 7：实现 c（锚点正移）**

`ZzCoreTerminalWidget.h:125` 签名加参：

```cpp
    /** @brief 读回桥注入完成后调用：重发射滚动范围/夹取偏移/锚点正移/重绘。
     *  @param prepended 本次实际注入持行数（capacity==0 即注即裁时为 0）。 */
    void notifyHistoryPrepended(int prepended);
```

`ZzCoreTerminalWidget.cpp:698` 实现：

```cpp
void ZzCoreTerminalWidget::notifyHistoryPrepended(int prepended)
{
    // 注入使统一行号顺移 +prepended：拖拽中的锚点副本同步正移（Core 侧
    // selection 已由 prependHistoryLines 正移；此副本在下次 mouseMove
    // 全量重放，不正移会把选区盖回漂移位置）。非拖拽态无害（下次
    // mousePress 重算）。
    selAnchor_.line += prepended;
    emit scrollRangeChanged(maxScrollOffset()); // 上限增长，滚动条扩展
    setScrollOffset(scrollOffset_);             // 夹取（数值不变即视口稳定）
    update();
}
```

`ZzScrollbackBridge.cpp:144-148` 调用点传实际注入行数：

```cpp
    const std::size_t histBefore = hv.lineCount();
    if (m_term->terminal().prependHistoryLines(std::move(lines)))
        m_term->notifyHistoryPrepended(
            int(m_term->terminal().historyView().lineCount() - histBefore));
```

（全仓搜 `notifyHistoryPrepended` 其它调用点/测试一并适配；视口稳定性已由既有口径保证：注入行占 [0,held)，旧内容顺移，同 scrollOffset 下视口恒等。）

- [ ] **步骤 8：运行验证通过 + 全量回归 + Commit（两个）**

```bash
git add src/terminal/ZzScrollbackBridge.cpp tests/unit/tst_ZzScrollbackBridge.cpp
git commit -m "fix(terminal): 读回注入宽字符行末边界丢弃，杜绝悬挂 WideLead 污染 reflow 链"
git add src/terminal/ZzScrollbackBridge.cpp src/terminal/ZzCoreTerminalWidget.h src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "fix(terminal): 读回注入时拖拽中选区锚点随内容正移"
```

---

## 任务 9：Core M20——HistoryView 增量 API（clearCount + resizeScreenScrollout）

**文件（全部在 `third_party/ZzTermCore` 子模块工作区，contour 分支）：**
- 修改：`include/ZzTerm/HistoryView.h`（clearCount 默认回退）、`include/ZzTerm/Terminal.h`（两个 facade 委托）
- 修改：`src/backend/ZzTerminalBackend.h`（resizeScreenScrollout 接口默认实现）
- 修改：`src/backend/native/ZzNativeBackend.h/.cpp`、`src/backend/native/NativeCsiDispatch.cpp`、`src/backend/native/ZzNativeHistoryView.h`（native 覆写）
- 测试：`tests/unit/test_historyview.cpp`（ZZ_TEST_EXPECT 宏风格，M19 测试同文件）
- 文档：`docs/API.md`（M20 条目，沿用 M18/M19 体例）

**背景：** 桥侧无法事后区分「重组旧历史」与「屏→史迁移行/清后新滚出行」（行身份已重建、终态不可区分），必须 Core 提供精确计数——与 M19 加 `primaryLineCount`（5735ff2）同一范式。两个 API 互不依赖但合并一次跨仓流程（一次 CI/pin）成本最低。**ABI 口径**：接口加带默认实现的虚函数，与 M19 同款，API.md 按既有体例登记。

- [ ] **步骤 1：编写失败的测试**

`tests/unit/test_historyview.cpp` 追加（沿用文件内 feedStr/historyLineText 助手与 ZZ_TEST_EXPECT 宏）：

```cpp
// ED3 清史计数：clearCount 单调递增（命令执行口径，空历史再清也计数）；
// clear 不复位 droppedLineCount（ChunkedScrollback.cpp:270 既有口径）。
static void test_clearCountIncrementedByEd3()
{
    ZzTerminal term(10, 3);
    feedStr(term, "l1\r\nl2\r\nl3\r\nl4\r\nl5\r\n");
    ZZ_TEST_EXPECT(term.historyView().clearCount() == 0);
    feedStr(term, "\x1b[3J");
    ZZ_TEST_EXPECT(term.historyView().clearCount() == 1);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    feedStr(term, "\x1b[3J");
    ZZ_TEST_EXPECT(term.historyView().clearCount() == 2);
}

// Alternate 期间 ED3 清的是 Primary 历史（NativeCsiDispatch 对 scrollback
// 无条件生效），clearCount 是 buffer 无关口径同样计入。
static void test_clearCountCountsEd3InAlternate()
{
    ZzTerminal term(10, 3);
    feedStr(term, "l1\r\nl2\r\nl3\r\nl4\r\n");
    feedStr(term, "\x1b[?1049h");
    feedStr(term, "\x1b[3J");
    ZZ_TEST_EXPECT(term.historyView().clearCount() == 1);
    feedStr(term, "\x1b[?1049l");
}

// 缩列屏→史迁移计数：40x6 屏喂 8 条 30 字符行（历史 3，屏 5 行+光标行），
// 缩列 24 后屏上行折叠溢出 5 行压入历史尾部，resizeScreenScrollout()==5；
// 下次 resize 入口清零重计。
static void test_resizeScreenScrolloutCountsMigratedLines()
{
    ZzTerminal term(40, 6);
    for (int i = 0; i < 8; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "segment-%d-abcdefghij0123456789\r\n", i);
        feedStr(term, buf);
    }
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 3);
    term.resize(24, 6);
    ZZ_TEST_EXPECT(term.resizeScreenScrollout() == 5);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 3 + 5);
}
```

（行数推演若与 Core 实际折行口径有出入，按实测调整字面量，断言结构不变；接缝回拉残差见步骤 3 披露。）

- [ ] **步骤 2：运行验证失败**（编译失败：两 API 未定义——预期红）

- [ ] **步骤 3：实现**

`include/ZzTerm/HistoryView.h`（类内追加，generation() 声明附近）：

```cpp
    /**
     * @brief 历史清空累计计数（ED 3 等；应用侧对账精确侦测用）。
     * @return 单调递增的清空次数（命令执行口径，空历史再清亦计数）。
     * @note 默认实现回退 0（无清空语义/未实现后端的保守降级，调用方退回收缩
     *       向兜底）；native 覆写为真实计数。Alternate 期间 ED3 清 Primary
     *       历史同样计入（buffer 无关）。
     */
    [[nodiscard]] virtual std::uint64_t clearCount() const noexcept { return 0; }
```

`src/backend/ZzTerminalBackend.h`（接口默认实现，contour 自动继承）：

```cpp
    /** @brief 上一次 resize 期间屏→史净迁移行数（历史尾部未落盘新行数）。 */
    [[nodiscard]] virtual std::size_t resizeScreenScrollout() const noexcept { return 0; }
```

`include/ZzTerm/Terminal.h` facade 委托（resizeScreenScrollout 一行委托 + 文档注释；clearCount 经 historyView() 暴露无需 facade）。

native 实现：
- `ZzNativeBackend` 加成员 `std::uint64_t historyClearCount_ = 0;`（声明在 `historyView_` 之前）+ `std::size_t resizeInflow_ = 0, resizeSeamPullback_ = 0; bool resizeActive_ = false;`
- `ZzNativeHistoryView` 构造加 `const std::uint64_t& clearCount` 引用成员（与既有 generation 引用同模式）并覆写 clearCount()
- `NativeCsiDispatch.cpp:111-114` ED3 处理处 `++historyGeneration_;` 旁加 `++historyClearCount_;`
- `ZzNativeBackend::resize()` 入口清零两计数并置 `resizeActive_`（出口复位）；ScrollOutCallback（:207-211）内 `if (resizeActive_) resizeInflow_ += lines.size();`；M16b 接缝回拉处（:280 一带）记 `resizeSeamPullback_ = histLines - head;`
- `resizeScreenScrollout()` 覆写返回 `resizeInflow_ > resizeSeamPullback_ ? resizeInflow_ - resizeSeamPullback_ : 0`

**接缝回拉减除的理由与已知残差（写进 commit message 与 API.md）：** 被拉回屏幕的 P 行历史尾链已落盘，溢出让它们最先回到历史（屏顶先溢出），净未落盘行 = 尾部 O−P 行。残差：链在新列宽重折后行数 P′ 可能与 P 不同（误差 ≤ 链长差，仅当 resize 瞬间有长逻辑行跨缝时触发）。若审查不接受，精化路径是 Screen 在 prependPrimaryLines 打标记、reflow 溢出时精确数首链归还行数——本里程碑不展开。

- [ ] **步骤 4：Core 全量回归 + API.md + Commit**

```bash
# 在 third_party/ZzTermCore 工作区（确认在 contour 分支）
ctest --test-dir build  # 或该仓惯例构建目录，基线 52/52
git add -A && git commit -m "feat(backend): M20 HistoryView 增量 API——clearCount 清史计数 + resizeScreenScrollout 迁移计数（默认回退+native 覆写，5735ff2 范式）"
```

- [ ] **步骤 5：跨仓门槛（用户参与）**

push contour → **等用户确认 Core CI 全绿** → 主仓 pin 前移：

```bash
cd third_party/ZzTermCore && git push origin contour
# CI 绿后回主仓：
git add third_party/ZzTermCore
git commit -m "chore(deps): ZzTermCore pin 前移（M20 clearCount + resizeScreenScrollout）"
```

---

## 任务 10：桥日志完整性修复（ED3 回锚 + resize 迁移行落盘）

**前置门槛：** 任务 9 完成且 pin 已前移（`historyView().clearCount()` 与 `terminal().resizeScreenScrollout()` 可用）。

**文件：**
- 修改：`src/terminal/ZzScrollbackBridge.h`（加 m_lastClearCount 成员 + appendScrolloutsUpTo 声明）、`src/terminal/ZzScrollbackBridge.cpp`
- 测试：`tests/unit/tst_ZzScrollbackBridge.cpp`

**机理（账本 :31/:32）：**
- ED3 同批：① 批内再滚动 ≥ 旧已落盘数时 `newest < seen` 不成立，收缩兜底看不见，差分从旧 idx 起追加，清后滚出行被吞；② 再滚动 < 旧已落盘数时锚到 `newest`——清后现存历史全是未落盘新行，正确锚点是 `dropped`。现有 `clearScrollbackDoesNotLoseLines` 绿只因 ED3 独占一个 feed（count=0 两锚点重合）。alt 内 ED3 清的是 Primary 历史，退出后首个主屏 feed 同洞。
- resize 缩列：屏上未落盘行折叠溢出追加到历史最新端（≤一屏），gridResized 回锚把这段尾部新行一并锚过，永久丢失。

- [ ] **步骤 1：抽共用例程（纯重构先行）**

`ZzScrollbackBridge.h` 私有区：

```cpp
void appendScrolloutsUpTo(std::uint64_t dropped, std::uint64_t count,
                          std::uint64_t newest);
```

`ZzScrollbackBridge.cpp`——contentFed 的 while 差分循环与缺口跳齐原样搬入：

```cpp
void ZzScrollbackBridge::appendScrolloutsUpTo(std::uint64_t dropped,
                                              std::uint64_t count,
                                              std::uint64_t newest)
{
    const auto &hv = m_term->terminal().historyView();
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
}
```

contentFed 改为调用该例程。全量回归（行为不变）。

```bash
git add src/terminal/ZzScrollbackBridge.h src/terminal/ZzScrollbackBridge.cpp
git commit -m "refactor(terminal): 桥滚动追加抽共用例程 appendScrolloutsUpTo（日志完整性修复前置）"
```

- [ ] **步骤 2：编写失败的测试（ED3 两条）**

```cpp
/**
 * @brief Z3 日志完整性 b①：ED3 与新输出同单批 feed，且批内滚动数 ≥
 *        旧已落盘数——收缩向兜底看不见（newest ≥ seen），修复前该批滚出行
 *        被旧游标锚过；clearCount 精确回锚后逐行落盘。
 */
void ed3SameBatchScrolloutNotSwallowed()
{
    ZzCoreTerminalWidget term;
    term.terminal().resize(10, 3);
    ZzLogEngine engine(ZzLogEngine::Config{});
    QVERIFY(engine.open());
    ZzScrollbackBridge bridge(&term, &engine);

    // 8 行进 3 行屏：历史 6（l1..l6 落盘），屏 [l7, l8, 空光标行]
    for (int i = 1; i <= 8; ++i)
        term.recvData(QStringLiteral("l%1\r\n").arg(i).toUtf8());
    QCOMPARE(engine.totalLines(), 6ULL);

    // 单批：ED3 + 8 行输出（批内滚动 8 行 > 旧已落盘 6 行）
    term.recvData(QByteArray(
        "\033[3Jq1\r\nq2\r\nq3\r\nq4\r\nq5\r\nq6\r\nq7\r\nq8\r\n"));
    // 滚出序：l7、l8（清后首批滚出的屏上旧行）+ q1..q6，共 8 行全落盘
    QCOMPARE(engine.totalLines(), 14ULL);
    QCOMPARE(logTexts(engine.getLines(6, 8)),
             QStringList({QStringLiteral("l7"), QStringLiteral("l8"),
                          QStringLiteral("q1"), QStringLiteral("q2"),
                          QStringLiteral("q3"), QStringLiteral("q4"),
                          QStringLiteral("q5"), QStringLiteral("q6")}));
}

/**
 * @brief Z3 日志完整性 b②：Alternate 内 ED3（清 Primary 历史）再退出，
 *        首个主屏 feed 的滚出行同类不吞（alt 守卫不排除 clearCount 回锚）。
 */
void ed3InAltThenExitDoesNotSwallowPrimaryScrollout()
{
    ZzCoreTerminalWidget term;
    term.terminal().resize(10, 3);
    ZzLogEngine engine(ZzLogEngine::Config{});
    QVERIFY(engine.open());
    ZzScrollbackBridge bridge(&term, &engine);

    for (int i = 1; i <= 8; ++i)
        term.recvData(QStringLiteral("l%1\r\n").arg(i).toUtf8());
    QCOMPARE(engine.totalLines(), 6ULL);

    term.recvData(QByteArray("\033[?1049h"));   // 进 Alternate
    term.recvData(QByteArray("\033[3J"));       // alt 内清 Primary 历史
    // 退 alt（1049l 恢复主屏）+ 足量输出（滚动 8 > 旧 6）
    term.recvData(QByteArray(
        "\033[?1049lq1\r\nq2\r\nq3\r\nq4\r\nq5\r\nq6\r\nq7\r\nq8\r\n"));
    QCOMPARE(engine.totalLines(), 14ULL);
    QCOMPARE(logTexts(engine.getLines(6, 8)),
             QStringList({QStringLiteral("l7"), QStringLiteral("l8"),
                          QStringLiteral("q1"), QStringLiteral("q2"),
                          QStringLiteral("q3"), QStringLiteral("q4"),
                          QStringLiteral("q5"), QStringLiteral("q6")}));
}
```

（滚动数推演：3 行屏 [l7, l8, 光标行]，q1..q8 逐行各挤一行，历史 = [l7, l8, q1..q6] 共 8 行。`logTexts` 若无现成助手，按文件内既有 getLines 用法就地写。）

- [ ] **步骤 3：运行验证失败**（两条 FAIL：totalLines 不足 14）

- [ ] **步骤 4：实现 ED3 精确回锚**

`ZzScrollbackBridge.h` 加成员 `std::uint64_t m_lastClearCount = 0;`；contentFed 改为：

```cpp
connect(m_term, &ZzCoreTerminalWidget::contentFed, this, [this]() {
    const auto &hv = m_term->terminal().historyView();
    const std::uint64_t dropped = hv.droppedLineCount();
    const std::size_t count = hv.lineCount();
    std::uint64_t newest = dropped + count;
    const std::uint64_t gen = hv.generation();
    // ED 3 精确侦测（Core clearCount）：清后现存历史全部未落盘，游标锚到
    // dropped——同批再滚动多少行都不依赖 newest/seen 比较；Alternate 期间
    // ED3 清的是 Primary 历史，同样生效（此处 count=0，追加循环自然空转，
    // 退出后首个主屏 feed 经差分全量补落盘）
    const std::uint64_t clears = hv.clearCount();
    if (clears != m_lastClearCount) {
        m_seenLineNo = dropped;
        m_lastClearCount = clears;
    } else if (gen != m_lastGeneration && newest < m_seenLineNo
               && !m_term->terminal().isAlternateScreen()) {
        // 无 clearCount 后端的收缩兜底：收缩=清史语义，锚 dropped 而非
        // newest——清后现存行皆是未落盘的新滚出行
        m_seenLineNo = dropped;
    }
    m_lastGeneration = gen;
    appendScrolloutsUpTo(dropped, count, newest);
});
```

**审查裁决点（写进任务报告）：** 收缩兜底锚点从 newest 改 dropped，是对无 clearCount 后端（contour）的语义收紧；注入路径无扰（M19 减法口径下 prepend 使 dropped 回退、seen 为绝对行号，清史后 dropped 不变）。

- [ ] **步骤 5：运行验证通过（ED3 两条 + 既有 12 用例全绿）**

- [ ] **步骤 6：编写失败的测试（resize 迁移行）**

```cpp
/**
 * @brief Z3 日志完整性 a：缩列 reflow 把屏上未落盘行压入历史尾部，
 *        gridResized 回锚前必须先落盘这段迁移行（修复前被锚过永久丢失）。
 */
void narrowReflowLogsMigratedScreenLines()
{
    ZzCoreTerminalWidget term;
    term.show();
    const int cw = term.cellWidthForTest();
    const int ch = term.cellHeightForTest();
    term.resize(40 * cw, 6 * ch);
    ZzLogEngine engine(ZzLogEngine::Config{});
    QVERIFY(engine.open());
    ZzScrollbackBridge bridge(&term, &engine);

    // 8 条 30 字符行进 6 行屏：历史 3（segment-0..2 落盘），
    // 屏 [segment-3..7, 空光标行]（与 narrowReflowDoesNotDuplicateLog 同构造）
    for (int i = 0; i < 8; ++i)
        term.recvData(
            QStringLiteral("segment-%1-abcdefghij0123456789\r\n").arg(i).toUtf8());
    QCOMPARE(engine.totalLines(), 3ULL);

    // 缩列 40 → 24：屏上 5 条 30 字符行各折 2 物理行 + 空行 = 11 > 6，
    // 溢出 5 行压入历史尾部（历史 3→11，其中尾部 5 行是屏上迁移行）
    term.resize(24 * cw, 6 * ch);
    QCOMPARE(term.terminal().historyView().lineCount(), std::size_t(11));

    // 修复核心断言：resize 当场（无后续 feed）迁移行已落盘——
    // 3 条旧历史 + 5 条迁移行（折叠新形态，物理行口径）
    QCOMPARE(engine.totalLines(), 8ULL);
    QCOMPARE(logTexts(engine.getLines(3, 5)),
             QStringList({QStringLiteral("segment-3-abcdefghij0123"),
                          QStringLiteral("456789"),
                          QStringLiteral("segment-4-abcdefghij0123"),
                          QStringLiteral("456789"),
                          QStringLiteral("segment-5-abcdefghij0123")}));

    // 落盘后游标已回锚：后续 feed 只追加真实 scrollout，迁移行不重复
    const quint64 engineBefore = engine.totalLines();
    term.recvData(QByteArray("n1\r\nn2\r\nn3\r\n"));
    const quint64 scrolled =
        quint64(term.terminal().historyView().lineCount()) - 11;
    QVERIFY(scrolled > 0);
    QCOMPARE(engine.totalLines(), engineBefore + scrolled);
}
```

（"segment-3-abcdefghij0123" 为 30 字符行在 24 列的前 24 字符，"456789" 为续行——若 Core 折行口径与此有出入按实测调整字面量，断言结构不变。widget resize 触发 gridResized 链路，故需 show + 像素尺寸构造，与 narrowReflowDoesNotDuplicateLog 同范式。）

- [ ] **步骤 7：运行验证失败**（FAIL：resize 当场 totalLines 仍为 3）

- [ ] **步骤 8：实现 gridResized 补落盘再回锚**

gridResized 回锚 lambda（:64-73）改为：

```cpp
connect(m_term, &ZzCoreTerminalWidget::gridResized, this,
        [this](int cols, int) {
            const auto &hv = m_term->terminal().historyView();
            const std::uint64_t gen = hv.generation();
            if (cols != m_lastCols && gen != m_lastGeneration) {
                const std::uint64_t dropped = hv.droppedLineCount();
                const std::uint64_t count = hv.primaryLineCount(); // buffer 无关口径
                const std::uint64_t newest = dropped + count;
                // 屏→史迁移行（reflow 溢出/缩行压历史，≤一屏）先落盘再回锚，
                // 否则这段未落盘行被锚过永久丢失；native lineAt 无 alt 守卫，
                // alt 期间亦可按 primaryLineCount 读 Primary 历史
                const std::uint64_t inflow = std::min<std::uint64_t>(
                    m_term->terminal().resizeScreenScrollout(), newest);
                m_seenLineNo = newest - inflow;
                appendScrolloutsUpTo(dropped, count, newest);
            }
            m_lastGeneration = gen;
            m_lastCols = cols;
        });
```

既有用例不受影响（已核对）：`narrowReflowDoesNotDuplicateLog`/`resizeWithBurstOutputLosesNoLines` 的 engineBefore 快照都在 resize 之后取；`reflowResetsAppendCursor`（扩列）溢出 0，inflow 钳 0 行为不变。

- [ ] **步骤 9：运行验证通过 + 全量回归 + Commit（两个）**

```bash
git add src/terminal/ZzScrollbackBridge.h src/terminal/ZzScrollbackBridge.cpp tests/unit/tst_ZzScrollbackBridge.cpp
git commit -m "fix(terminal): ED3 清史经 Core clearCount 精确回锚，同批滚出行不再被吞"
git commit -m "fix(terminal): resize 屏→史迁移行回锚前补落盘（gridResized 经 resizeScreenScrollout）"
```

（两 fix 文件重叠可分两轮 add/commit；第二轮测试全绿后桥用例从 12 增至 15。）

---

## 任务 11：Alt 屏滚轮收紧 + 选区鼠标仿真用例

**文件：**
- 测试：`tests/unit/tst_ZzCoreTerminalWidget.cpp`（改写 alternateScreenWheelSendsArrowKeys；新增两用例）

**背景：** d) 现用例 `all.count('A') >= 3` 放行多发、只测上滚、1049l 后无 Primary 恢复断言；每档 3 次语义在 wheelEvent `qAbs(steps)*3`，编码侧 DECCKM 默认关（InputEncoder.cpp:87-93）可做精确字节断言。c) spike 时代两个回归（锚点重放、画笔外泄）移植时未随行，主仓对应机制（mouseMoveEvent 锚点重放 :768-779、越缘横滚 :762-766、画笔 save/restore :371/:415）零覆盖。移植口径差异：最小网格钳制 24×6（resize 24 列）、拼接行显示行公式 `row = gridRows - lineCount`、offscreen 剪贴板已被 spike 实证可用。

- [ ] **步骤 1：收紧 alternateScreenWheelSendsArrowKeys（整函数替换）**

```cpp
void alternateScreenWheelSendsArrowKeys()
{
    ZzCoreTerminalWidget widget;
    widget.terminal().resize(10, 4);
    for (int i = 0; i < 10; ++i) // 造历史，供 1049l 后 Primary 滚轮断言
        widget.recvData(QByteArray("line\r\n"));
    QVERIFY(widget.maxScrollOffset() > 0);

    widget.recvData(QByteArray("\x1b[?1049h")); // 进 Alternate 屏
    QSignalSpy spy(&widget, &ZzCoreTerminalWidget::sendData);
    const auto sendWheel = [&widget](int deltaY) {
        QWheelEvent e(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0),
                      QPoint(0, deltaY), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
        QApplication::sendEvent(&widget, &e);
    };
    const auto drained = [&spy] {
        QByteArray all;
        for (const auto &call : spy)
            all += call.at(0).toByteArray();
        return all;
    };

    sendWheel(120); // 上滚一档 = 恰 3 次 ↑（DECCKM 默认关，CSI A 形式）
    QCOMPARE(drained(), QByteArray("\x1b[A").repeated(3));

    spy.clear();
    sendWheel(-120); // 下滚对称：恰 3 次 ↓
    QCOMPARE(drained(), QByteArray("\x1b[B").repeated(3));

    widget.recvData(QByteArray("\x1b[?1049l")); // 回 Primary
    spy.clear();
    sendWheel(120);
    QCOMPARE(spy.count(), 0);           // Primary 滚轮不再发键
    QCOMPARE(widget.scrollOffset(), 3); // 恢复滚历史语义（每档 3 行）
}
```

- [ ] **步骤 2：新增鼠标仿真两用例**

```cpp
void unwrapMouseDragCopiesFullLine()
{
    // 回归 spike case 8（锚点重放 + 越缘横滚）：鼠标拖拽选中超宽行，
    // 复制含屏外部分。钳制 24 列下 40 字符行超宽 16 列（前提自证）。
    ZzCoreTerminalWidget widget;
    widget.show(); // 隐藏 widget 的 resize() 不投递 QResizeEvent
    const int cw = widget.cellWidthForTest();
    const int ch = widget.cellHeightForTest();
    widget.resize(24 * cw, 6 * ch);
    QCOMPARE(widget.gridCols(), 24);
    const QByteArray line = QByteArray("0123456789ABCDEFGHIJ").repeated(2); // 40 列
    widget.recvData(line + "\r\n");
    widget.setUnwrapMode(true);
    QCOMPARE(widget.hScrollOffset(), 0); // "\r\n" 后光标回列 0，横向跟随不外溢
    // 拼接行 0 的显示行 = rows - lineCount（钉底视口，scrollOffset=0）
    const int rowY = (widget.gridRows()
                      - (int)widget.terminal().unwrapView().lineCount()) * ch + 1;

    // 手势一：窗口内拖拽，按下锚点在拖动中保持（引擎空选区 extend 语义会吞锚点）
    QTest::mousePress(&widget, Qt::LeftButton, {}, QPoint(1, rowY));
    QTest::mouseMove(&widget, QPoint(23 * cw + 1, rowY));
    QVERIFY2(widget.terminal().hasSelection(), "拖拽中应保持按下锚点的选区");
    QTest::mouseRelease(&widget, Qt::LeftButton, {}, QPoint(23 * cw + 1, rowY));
    QCOMPARE(QGuiApplication::clipboard()->text(),
             QString::fromUtf8(line.left(24))); // [0,24)

    // 手势二：拖拽越过右缘横向步进至钳制上限 16，全行 [0,40) 入剪贴板
    QTest::mousePress(&widget, Qt::LeftButton, {}, QPoint(1, rowY));
    for (int i = 0; i < 17; ++i) // 每次 move 步进一格，钳到 maxHScrollOffset=16
        QTest::mouseMove(&widget, QPoint(24 * cw + 5, rowY));
    QCOMPARE(widget.hScrollOffset(), 16);
    QTest::mouseRelease(&widget, Qt::LeftButton, {}, QPoint(24 * cw + 5, rowY));
    QCOMPARE(QGuiApplication::clipboard()->text(), QString::fromUtf8(line));
}

void selectionHighlightPenDoesNotLeak()
{
    // 回归 spike case 9（画笔外泄）：选区高亮的 highlightedText 画笔须
    // save/restore，选中行下方行仍以 cell 前景色绘制（grab 像素级验证）。
    ZzCoreTerminalWidget widget;
    const int cw = widget.cellWidthForTest();
    const int ch = widget.cellHeightForTest();
    widget.resize(24 * cw, 6 * ch);
    widget.recvData(QByteArray("aaaaa\r\nbbbbb\r\n")); // 行 0/行 1
    QTest::mousePress(&widget, Qt::LeftButton, {}, QPoint(1, 1));
    QTest::mouseMove(&widget, QPoint(2 * cw + 1, 1)); // 选中行 0 [0,3)
    QTest::mouseRelease(&widget, Qt::LeftButton, {}, QPoint(2 * cw + 1, 1));

    const QImage img = widget.grab().toImage();
    const QColor bg = img.pixelColor(cw / 2, 5 * ch + ch / 2); // 空行底色
    int ink = 0;
    for (int y = ch + 1; y < 2 * ch - 1; ++y)
        for (int x = 1; x < 5 * cw; ++x)
            if (img.pixelColor(x, y) != bg)
                ++ink;
    QVERIFY2(ink > 0, "选区下方行文本不可见：高亮画笔外泄");
}
```

（越缘算术：unwrap 下 `col = qBound(0, x/cw, 23) + hScrollOffset_`，锚点钳 23 + 横滚 16 = 39，extent+1=40 恰好全行——spike 同构路径实证。）

- [ ] **步骤 3：运行验证**

三条都是钉板既有行为，预期 PASS；FAIL 即真回归（收紧断言暴露多发/漏发属预期发现，按实际行为修实现或回调断言并记录裁定）。

- [ ] **步骤 4：全量回归 + Commit**

```bash
git add tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "test(terminal): Alt 屏滚轮精确字节断言（上下滚+Primary 恢复）+ 选区鼠标仿真/画笔外泄回归"
```

---

## 任务 12：宽字符续格光标重绘

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.cpp`（paintWrapped 光标块 :254-270）
- 测试：`tests/unit/tst_ZzCoreTerminalWidget.cpp`

**背景：** 0367981 两遍制后字形先画、光标块后铺。光标会落在宽字符续格上（两条 Core 实证路径：宽字符写满行尾时引擎停末列续格并置 wrap-pending，`ZzNativeBackend.cpp:462-464`，80 列屏敲 40 个汉字即触发；CSI CUP 显式定位不做宽度对齐），续格上的 fillRect 把左格字形右半盖成纯色块，续格 text 为空、既有重绘分支不救。paintUnwrapped 光标本来就不重绘任何字形（最简口径），不在本任务——登记为后续。

- [ ] **步骤 1：编写失败的测试（像素级，仿 wideCharInkSpansContinuationCell）**

```cpp
void blockCursorOnWideContinuationKeepsGlyphInk()
{
    // 回归：宽字符写满行尾时引擎停光标于末列续格（wrap-pending），块状
    // 光标盖住左格字形右半。修复后以反色画笔从首格原点重绘整字——
    // 续格内缘须出现背景色笔画像素（修复前整块纯色，计数为 0）。
    ZzCoreTerminalWidget widget;
    widget.recvData(QStringLiteral("中").repeated(40).toUtf8()); // 80 格满，光标停格79
    const QImage img = widget.grab().toImage();
    const int cw = widget.cellWidthForTest();
    const int ch = widget.cellHeightForTest();
    const QColor bg = img.pixelColor(cw / 2, 4 * ch + ch / 2); // 远处纯背景
    const QRect cont(79 * cw + 1, 1, cw - 2, ch - 2); // 格79（续格）内缘
    int bgInk = 0;
    for (int y = cont.top(); y <= cont.bottom(); ++y)
        for (int x = cont.left(); x <= cont.right(); ++x)
            if (img.pixelColor(x, y) == bg)
                ++bgInk;
    QVERIFY2(bgInk > 0, "光标盖住宽字符右半：续格内无反色笔画");
}
```

（默认网格 80×24；若 offscreen 默认控件宽度不足 80 格导致光标不在格 79，先 `widget.resize(80 * cw, 6 * ch)`，网格不受 resize 影响；若光标闪烁使 grab 时序敏感，参照既有像素用例的处置。）

- [ ] **步骤 2：运行验证失败**（FAIL：bgInk==0）

- [ ] **步骤 3：实现**

paintWrapped 光标块内（fillRect 之后的重绘段，:262-269 一带）替换为：

```cpp
        painter.fillRect(cursorRect, resolveCellColor(ZzColor::Default(), true));
        const ZzLineView cursorLine = view.lineAt(cursor.position.row);
        const ZzCellView under = cursorLine.cellAt(cursor.position.col);
        painter.setPen(resolveCellColor(ZzColor::Default(), false));
        const int textY = cursor.position.row * cellHeight_ + cellAscent_;
        if (under.width == ZzCellWidth::WideContinuation
            && cursor.position.col > 0) {
            // 光标落在宽字符续格（行尾 wrap-pending 或 CSI CUP 定位）：
            // 块盖住左格字形右半，从首格原点以反色重绘整字，右半笔画
            // 盖回光标块上（与窄字符「重绘该格文本」同口径）。
            const ZzCellView lead = cursorLine.cellAt(cursor.position.col - 1);
            if (!lead.text.empty())
                painter.drawText((cursor.position.col - 1) * cellWidth_, textY,
                                 QString::fromStdString(lead.text));
        } else if (!under.text.empty()) {
            painter.drawText(cursor.position.col * cellWidth_, textY,
                             QString::fromStdString(under.text));
        }
```

（`ZzCellView.width` 见 `ZzTermCore/include/ZzTerm/RenderView.h:23`，现成可用，零 Core 改动。）

- [ ] **步骤 4：运行验证通过 + 全量回归 + Commit**

```bash
git add src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "fix(terminal): 块状光标落宽字符续格时从首格反色重绘整字，不再盖掉字形右半"
```

---

## 任务 13：登记与收官

**文件：**
- 修改：`docs/Scrollback-and-Reflow.md`（contour 启用清单）
- 修改：`.superpowers/sdd/2026-10-13-z3-deferred-cleanup/progress.md`（账本核销与新登记）
- 修改：`CMakeLists.txt`（版本 0.2.0 → 0.3.0）

- [ ] **步骤 1：contour 启用清单登记（账本 :33 条目 c 决议：不修只登记）**

不修依据（写进文档）：① 产品唯一构造点 `ZzCoreTerminalWidget.cpp:65` 写死 `ZzBackendKind::Native`，contour 不可达；② contour 下读回路径本就不工作（`prependHistoryLines` 对 contour 拒绝，`HistoryView.h:71`），启用 contour 是一整包工作不是单点修补；③ M20 两个新 API 都是默认回退 0 形态，contour 行为不因 Z3 改变（不好不坏）。

`docs/Scrollback-and-Reflow.md` 追加一节「contour 后端启用清单」：启用 contour 时需一并落地——`primaryLineCount` 覆写、`clearCount` 覆写、`resizeScreenScrollout` 覆写、`prependHistoryLines` 支持；并指回 Core `docs/API.md` 对应条目。

- [ ] **步骤 2：新登记 + 账本核销**

账本登记：
- 新延后项：`paintUnwrapped` 光标不重绘任何字形（最简口径，与 wrapped 路径不对称）——后续里程碑评估；
- Z2 账本 23 条逐条核销（指向 Z3 各任务 commit 或任务 3 的核销证据）；
- 接缝回拉 P/P′ 精度残差（任务 9）保留为已知边界记录。

- [ ] **步骤 3：全量回归 + 版本号 + Commit**

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux-gcc-debug  # 51+新增 全绿
```

`CMakeLists.txt`：`project(ZzClawTerm VERSION 0.3.0 ...)`（会话级配色是新用户可见能力，minor 递增）。

```bash
git add docs/Scrollback-and-Reflow.md .superpowers CMakeLists.txt
git commit -m "chore: Z3 收口登记 + 版本 0.3.0"
```

- [ ] **步骤 4：用户手测验收（含 Z2 遗留项）**

1. **会话级配色**（任务 1 口径）：配置页选配色开会话生效；全局切换不冲会话级；「跟随全局」回退正确；
2. **Windows GBK**（Z2 账本手测提醒）：Windows 上 GBK 会话收发中文不乱码（Core5Compat 路径）；
3. **IME 连续上屏**：中文输入法连续打字，preedit 不丢拍；不换行模式下 preedit 可见；
4. **触控板/精密滚轮**：小步距慢滚不再偏钝（无 hi-res 设备则跳过）；
5. **宽字符行尾光标**：80 列屏连续敲 40 个汉字，行尾光标不盖字形右半；
6. **越顶读回**（Z2 任务 5 跳过项）：长会话滚到顶继续上滚逐块加载，resize 后不错位不重行——重点验证日志完整性修复后无丢行；
7. **最小网格**（Z2 任务 8 跳过项）：窗口最小尺寸限制下行为确认即可（程序限制在前，视为通过）。

- [ ] **步骤 5：终审 + tag**

SDD 终审（规格覆盖/跨任务链路/延后项零残留）→ push → CI 绿后打 tag：ZzClawTerm `v0.3.0`；Core 侧若任务 9 落地则打 `m20`。

---

## 自检记录

- **规格覆盖**：Z2 账本 23 条 + 手测新发现 2 条 + 手测遗留 3 项，全部映射到任务（见文首总表）；任务 9/10 覆盖账本 :31/:32/:33 三条，:33 决议不修只登记（理由在任务 13 步骤 1）。
- **占位符扫描**：所有代码步骤含实际代码块；仅有的两处「按实测调整字面量」是折行口径的显式披露（断言结构不变），非占位。
- **类型一致性**：`appendScrolloutsUpTo`（任务 10 定义，任务 10 两修复共用）；`notifyHistoryPrepended(int)`（任务 8 改签名，调用点同任务适配）；`clearCount()`/`resizeScreenScrollout()`（任务 9 Core 定义，任务 10 主仓消费，pin 前移为门槛）；`setSessionColorScheme`/`m_sessionColorScheme`（任务 1 定义并消费）；`wheelRemainder_`（任务 7 定义并消费）。
- **冲突消解**：tst_ZzCoreTerminalWidget.cpp 被任务 2/6/7/8/11/12 共用、ZzScrollbackBridge.cpp 被任务 8/10 共用——SDD 串行执行，任务内 commit 收尾，无并行冲突。
