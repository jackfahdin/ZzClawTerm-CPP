# ZzPureTools 升级与 ZzTermWidget 移除实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 先修 ZzPureTools superbuild 阻断并升级到最新 master，显式接入 Stacked 中央工作区，随后清理测试残留并彻底删除 ZzTermWidget。

**架构：** ZzPureTools 继续以静态 `add_subdirectory` 方式消费；上游修复路径问题后，主仓只记录新的子模块 pin，不长期绕行。工作区显式使用 Stacked，旧布局恢复失败时清空一次。ZzTermWidget 删除前先让禁用测试回到当前架构，避免留下指向已删除组件的源码。

**技术栈：** CMake/Ninja、Qt 6.11.1 Widgets/QTest、Git submodule、ZzPureTools/QWindowKit、ZzTermCore m20。

---

## 文件结构

- `third_party/ZzPureTools/ZzPureTools/CMakeLists.txt`：上游组件构建定义；修复 superbuild 下私有头搜索路径。
- `third_party/ZzPureTools`：主仓 gitlink；记录修复后的最新上游 master。
- `src/ZzAppShell.cpp`：WorkspaceShell 创建模式、布局恢复失败清理、标题注释口径。
- `src/main.cpp`：删除已由上游修复接管的标题重放 workaround。
- `tests/unit/tst_ZzAppShell.cpp`：锁定 Stacked 模式和旧布局一次性清理。
- `tests/CMakeLists.txt`：恢复 `tst_ZzSplitContainer`、`tst_ZzConnectFlow`、`tst_ZzSettingsPage` 注册。
- `tests/unit/tst_ZzConnectFlow.cpp`：改用当前 `sendData(QByteArray)` 信号。
- `tests/unit/tst_ZzSplitContainer.cpp`：更新焦点代理注释。
- `tests/perf/tst_PerfScrollback.cpp`：改用 `ZzCoreTerminalWidget + ZzScrollbackBridge` 的真实 scrollout 落盘链路。
- `tests/perf/CMakeLists.txt`：恢复 `tst_PerfScrollback` 注册。
- `.gitmodules`、`README.md`、`CMakeLists.txt`、`src/log/ZzLogLine.h`：删除 ZzTermWidget 元数据与活跃说明残留。

## 任务 1：修复 ZzPureTools superbuild include 路径

**文件：**
- 修改：`third_party/ZzPureTools/ZzPureTools/CMakeLists.txt`

- [ ] **步骤 1：切到 ZzPureTools master 并对齐最新提交**

```bash
git -C third_party/ZzPureTools switch master
git -C third_party/ZzPureTools pull --ff-only origin master
git -C third_party/ZzPureTools rev-parse HEAD
```

预期：HEAD 至少为 `e46bee18dde152edd15d1bfe687d2dc979b96f70`；工作区干净。

- [ ] **步骤 2：用主仓 superbuild 复现失败**

运行：

```bash
QT_ROOT=/home/zz/Qt/6.11.1/gcc_64 \
cmake -S . -B build/zzpuretools-superbuild-red -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/home/zz/Qt/6.11.1/gcc_64
cmake --build build/zzpuretools-superbuild-red --target ZzPureTools --parallel
```

预期：构建失败，错误来自 `ZzWorkspaceWindowCoordinatorPrivate.cpp` 包含
`<private/ZzWorkspacePageIdentityPrivate.h>` 或
`<private/ZzSplitWorkspaceTransactionPrivate.h>`；原因是 include 路径错指到
ZzClawTerm 根目录。

- [ ] **步骤 3：修复上游 include 路径**

把 `third_party/ZzPureTools/ZzPureTools/CMakeLists.txt` 中：

```cmake
target_include_directories(ZzPureTools PRIVATE
    "${CMAKE_SOURCE_DIR}/ZzFluentUI/widgets/src")
```

改为：

```cmake
target_include_directories(ZzPureTools PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../ZzFluentUI/widgets/src")
```

`CMAKE_CURRENT_SOURCE_DIR` 在组件级 CMakeLists 中固定指向
`third_party/ZzPureTools/ZzPureTools`，同级 `../ZzFluentUI/widgets/src` 才是
真实私有头目录。

- [ ] **步骤 4：验证红灯转绿**

运行：

```bash
cmake --build build/zzpuretools-superbuild-red --target ZzPureTools --parallel
```

预期：`ZzPureTools` 构建成功。随后运行上游自身构建烟测：

```bash
QT_ROOT=/home/zz/Qt/6.11.1/gcc_64 \
cmake -S third_party/ZzPureTools -B build/zzpuretools-upstream-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/home/zz/Qt/6.11.1/gcc_64 \
  -DZZ_BUILD_TESTS=ON -DZZ_BUILD_EXAMPLES=OFF -DZZ_BUILD_BENCHMARKS=OFF
cmake --build build/zzpuretools-upstream-debug --target ZzPureTools --parallel
```

预期：退出码 0。

- [ ] **步骤 5：提交并推送上游修复**

```bash
git -C third_party/ZzPureTools add ZzPureTools/CMakeLists.txt
git -C third_party/ZzPureTools commit -m "fix(构建): superbuild 私有头路径改用组件源码根"
git -C third_party/ZzPureTools push origin master
```

预期：上游 master 新增一个修复提交；主仓 `git status` 显示
`third_party/ZzPureTools` 有新 gitlink，先不提交。

## 任务 2：升级主仓 ZzPureTools pin

**文件：**
- 修改：`third_party/ZzPureTools`（gitlink）

- [ ] **步骤 1：确认子模块位于刚推送的上游 master**

```bash
git -C third_party/ZzPureTools fetch origin master
git -C third_party/ZzPureTools merge --ff-only origin/master
git -C third_party/ZzPureTools log -3 --oneline --decorate
```

预期：最后一个提交是任务 1 的上游修复。

- [ ] **步骤 2：全新目录配置并完整构建**

不复用任何已有构建目录，避免旧 `qmsetup_DIR` 缓存掩盖 QWindowKit 构建链变化：

```bash
QT_ROOT=/home/zz/Qt/6.11.1/gcc_64 \
cmake -S . -B build/zzpuretools-latest-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/home/zz/Qt/6.11.1/gcc_64
cmake --build build/zzpuretools-latest-debug --parallel
```

预期：主仓和全部测试目标构建成功，退出码 0。

- [ ] **步骤 3：运行现有全量测试**

```bash
QT_QPA_PLATFORM=offscreen \
ctest --test-dir build/zzpuretools-latest-debug --output-on-failure
```

预期：51/51 通过。若出现编译或测试失败，只在本任务内做使现有契约恢复的最小适配；不引入新功能。

- [ ] **步骤 4：提交 pin 升级**

```bash
git add third_party/ZzPureTools
git commit -m "chore(deps): 升级 ZzPureTools 至最新 master"
```

## 任务 3：显式 Stacked 与旧布局一次性清理

**文件：**
- 修改：`src/ZzAppShell.cpp`
- 修改：`src/main.cpp`
- 测试：`tests/unit/tst_ZzAppShell.cpp`

- [ ] **步骤 1：添加失败测试**

在 `tests/unit/tst_ZzAppShell.cpp` 的 `assembleCreatesWorkspaceAndStatusBar()`
中，`QVERIFY(shell.workspaceShell() != nullptr);` 后加入：

```cpp
        // Stacked 中央工作区：无框架外层标签/分屏容器，页面由堆叠容器承载
        QVERIFY(shell.workspaceShell()->stackWidget() != nullptr);
        QVERIFY(shell.workspaceShell()->tabWidget() == nullptr);
        QVERIFY(shell.workspaceShell()->splitWorkspace() == nullptr);
```

在 `doubleClickOpensTabAndUpdatesStatusBar()` 前新增：

```cpp
    void invalidWorkspaceLayoutIsCleared()
    {
        auto &settings = ZzAppSettings::instance();
        settings.setWorkspaceLayout(QByteArray("legacy-tabbed-layout"));

        ZzAppShell shell(m_dir);
        QMainWindow window;
        QVERIFY(shell.assemble(window));
        QCOMPARE(settings.workspaceLayout(), QByteArray());
    }
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build build/zzpuretools-latest-debug --target tst_ZzAppShell --parallel
QT_QPA_PLATFORM=offscreen \
build/zzpuretools-latest-debug/tests/tst_ZzAppShell invalidWorkspaceLayoutIsCleared
```

预期：`invalidWorkspaceLayoutIsCleared` 失败，恢复失败后设置仍保留
`legacy-tabbed-layout`。

- [ ] **步骤 3：显式 Stacked 并清理失效布局**

在 `src/ZzAppShell.cpp` 增加：

```cpp
#include <ZzPureTools/ZzWorkspaceCenterMode.h>
```

把：

```cpp
    auto created = ZzPureTools::ZzWorkspaceShell::create(&window, titleBar);
```

改为：

```cpp
    auto created = ZzPureTools::ZzWorkspaceShell::create(
        &window, titleBar, ZzPureTools::ZzWorkspaceCenterMode::Stacked);
```

把布局恢复失败分支：

```cpp
        if (!restored) {
            qWarning().noquote() << QStringLiteral(
                "工作区布局恢复失败，使用默认布局：%1")
                .arg(restored.error().technicalMessage());
        }
```

改为：

```cpp
        if (!restored) {
            qWarning().noquote() << QStringLiteral(
                "工作区布局恢复失败，清空旧布局并使用默认布局：%1")
                .arg(restored.error().technicalMessage());
            // Stacked 不兼容旧 Tabbed 布局；清空后避免每次启动重复失败
            ZzAppSettings::instance().setWorkspaceLayout({});
        }
```

- [ ] **步骤 4：移除已过期的标题重放 workaround**

上游新版本已保留应用自定义标题。删除 `src/main.cpp` 中：

```cpp
    // 框架窗口初始化在 assemble() 之后还会执行 refreshTranslations()，把窗口与
    // Fluent 标题栏标题重置为库默认「ZzPureTools」（ZzApplicationWindowPrivate），
    // 覆盖装配期设置的标题。build 完成后重放一次标题刷新夺回设置（setApplicationTitle
    // 无条件触发 refreshTitle，幂等）。
    shell.workspaceShell()->setApplicationTitle(
        QStringLiteral("ZzClawTerm"));
```

并把 `src/ZzAppShell.cpp` 中“库默认固化为 ZzPureTools”的注释改成：

```cpp
    // 窗口标题策略：当前页标题 - 应用名；当前页标题由页面 windowTitle 提供，
    // 终端页同步为会话名（见 ZzTabManager::refreshWindowTitle）。
```

- [ ] **步骤 5：验证测试通过**

```bash
cmake --build build/zzpuretools-latest-debug --parallel
QT_QPA_PLATFORM=offscreen \
build/zzpuretools-latest-debug/tests/tst_ZzAppShell
QT_QPA_PLATFORM=offscreen \
ctest --test-dir build/zzpuretools-latest-debug --output-on-failure
```

预期：`tst_ZzAppShell` 全部通过；全量 51/51 通过。若标题断言失败，说明
上游标题修复覆盖不到主仓路径，恢复 `main.cpp` 重放块并把该差异记录到
commit message；不得保留红测试。

- [ ] **步骤 6：提交**

```bash
git add src/ZzAppShell.cpp src/main.cpp tests/unit/tst_ZzAppShell.cpp
git commit -m "feat(shell): 启用 Stacked 中央工作区并清理失效旧布局"
```

## 任务 4：恢复三个旧组件禁用单测

**文件：**
- 修改：`tests/CMakeLists.txt`
- 修改：`tests/unit/tst_ZzConnectFlow.cpp`
- 修改：`tests/unit/tst_ZzSplitContainer.cpp`
- 测试：`tests/unit/tst_ZzSettingsPage.cpp`

- [ ] **步骤 1：恢复注册并删除旧组件 include**

在 `tests/CMakeLists.txt` 中，把三处注释块分别替换为：

```cmake
zz_add_qtest(tst_ZzSplitContainer unit/tst_ZzSplitContainer.cpp)
zz_add_qtest(tst_ZzConnectFlow unit/tst_ZzConnectFlow.cpp)
zz_add_qtest(tst_ZzSettingsPage unit/tst_ZzSettingsPage.cpp)
```

从 `tests/unit/tst_ZzConnectFlow.cpp` 删除：

```cpp
#include "qtermwidget.h"
```

- [ ] **步骤 2：按当前信号口径适配连接流程测试**

把 `tests/unit/tst_ZzConnectFlow.cpp` 中：

```cpp
        emit view->termWidget()->sendData("pwd\n", 4);
```

改为：

```cpp
        emit view->termWidget()->sendData(QByteArray("pwd\n"));
```

- [ ] **步骤 3：更新分屏焦点注释**

把 `tests/unit/tst_ZzSplitContainer.cpp` 中：

```cpp
        a->setFocus(); // 经 QTermWidget 焦点代理落到内部显示件
```

改为：

```cpp
        a->setFocus(); // 经 ZzTerminalView 焦点代理落到内部终端控件
```

- [ ] **步骤 4：构建并运行恢复测试**

```bash
cmake --build build/zzpuretools-latest-debug --parallel
QT_QPA_PLATFORM=offscreen \
ctest --test-dir build/zzpuretools-latest-debug --output-on-failure \
  -R '^(tst_ZzSplitContainer|tst_ZzConnectFlow|tst_ZzSettingsPage)$'
```

预期：3/3 通过。若某个恢复测试失败，只按当前生产接口做最小适配，并在
commit message 中说明实际适配点。

- [ ] **步骤 5：运行全量测试**

```bash
QT_QPA_PLATFORM=offscreen \
ctest --test-dir build/zzpuretools-latest-debug --output-on-failure
```

预期：54/54 通过。

- [ ] **步骤 6：提交**

```bash
git add tests/CMakeLists.txt tests/unit/tst_ZzConnectFlow.cpp \
  tests/unit/tst_ZzSplitContainer.cpp tests/unit/tst_ZzSettingsPage.cpp
git commit -m "test(shell): 恢复旧终端组件摘除后的三个单测"
```

## 任务 5：重写滚动历史性能门控

**文件：**
- 修改：`tests/perf/tst_PerfScrollback.cpp`
- 修改：`tests/perf/CMakeLists.txt`
- 测试：`tests/unit/tst_ZzScrollbackBridge.cpp`（只读参照，不改）

- [ ] **步骤 1：用新链路重写测试**

把 `tests/perf/tst_PerfScrollback.cpp` 全文替换为：

```cpp
#include <QtTest/QtTest>

#include <QTemporaryDir>

#include "ZzPerfRecorder.h"
#include "log/ZzLogEngine.h"
#include "terminal/ZzCoreTerminalWidget.h"
#include "terminal/ZzScrollbackBridge.h"

/**
 * @brief 性能门控：真实终端 scrollout 链路向引擎追加 10 万行
 *        （Core 解析 + 桥差分 + 热层/温层归档路径，Release）。
 * @note 滚动帧时间由渲染链路另行覆盖；此处门控的是「输出行 → 引擎」
 *       装配链路不成为吞吐瓶颈。
 */
class tst_PerfScrollback : public QObject
{
    Q_OBJECT
private slots:
    void appendHundredThousandLines()
    {
        if (!ZzPerfRecorder::gatingEnabled()) {
            QSKIP("性能门控仅在 Release 构建下有效（规格 §9.1）");
        }
        ZzCoreTerminalWidget term;
        term.terminal().resize(120, 25); // 行宽足够，短行不折行；25 行屏留下 24 行
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ZzLogEngine::Config config; // 热 1 万 / 温 100 万 → 走归档路径
        config.warmFilePath = dir.filePath(QStringLiteral("warm.log"));
        ZzLogEngine engine(config);
        QVERIFY(engine.open());
        ZzScrollbackBridge bridge(&term, &engine);

        QElapsedTimer timer;
        timer.start();
        for (int block = 0; block < 1000; ++block) {
            QByteArray chunk;
            for (int i = 0; i < 100; ++i) {
                chunk += QStringLiteral("perf-line-%1 abcdefghijklmnopqrstuvwxyz\r\n")
                    .arg(block * 100 + i).toUtf8();
            }
            term.recvData(chunk); // 分块喂入，避免单次超过 Core 历史窗口导致跳齐
        }
        QByteArray tail;
        for (int i = 100000; i < 100024; ++i)
            tail += QStringLiteral("perf-line-%1 abcdefghijklmnopqrstuvwxyz\r\n")
                .arg(i).toUtf8();
        term.recvData(tail);
        engine.flush(); // 等待已排队批次归档完成后再计时与核对总行数
        const double elapsed = static_cast<double>(timer.elapsed());
        QCOMPARE(engine.totalLines(), 100000ULL);

        const bool ok = ZzPerfRecorder::recordAndCheck(
            QStringLiteral("scrollback-append"),
            QStringLiteral("滚动历史追加 10 万行"), 5000.0, elapsed);
        QVERIFY2(ok, qPrintable(QStringLiteral("实测 %1ms 超过阈值 5000ms").arg(elapsed)));
    }
};

QTEST_MAIN(tst_PerfScrollback)
#include "tst_PerfScrollback.moc"
```

在 `tests/perf/CMakeLists.txt` 中，把注释掉的注册行替换为：

```cmake
zz_add_perf_test(tst_PerfScrollback tst_PerfScrollback.cpp)
```

- [ ] **步骤 2：先跑 Debug 验证语义**

```bash
cmake --build build/zzpuretools-latest-debug --target tst_PerfScrollback --parallel
QT_QPA_PLATFORM=offscreen \
build/zzpuretools-latest-debug/tests/perf/tst_PerfScrollback
```

预期：Debug 下因 `gatingEnabled()` 为 false 而 QSKIP；这是既有门控口径。

- [ ] **步骤 3：全新 Release 构建并实测阈值**

```bash
QT_ROOT=/home/zz/Qt/6.11.1/gcc_64 \
cmake -S . -B build/zzpuretools-latest-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/home/zz/Qt/6.11.1/gcc_64
cmake --build build/zzpuretools-latest-release --target tst_PerfScrollback --parallel
QT_QPA_PLATFORM=offscreen \
build/zzpuretools-latest-release/tests/perf/tst_PerfScrollback
```

预期：总行数必须为 100000。若耗时超过 5000ms，先连续运行 3 次确认不是
首次归档/磁盘抖动；若稳定超限，记录 3 次耗时并把阈值调整为「三次最大值
向上取整后再上浮 20%」，同时更新断言文案中的阈值。不得在不确认总行数的
情况下放宽阈值。

- [ ] **步骤 4：运行 Release 全量与 Debug 全量**

```bash
cmake --build build/zzpuretools-latest-release --parallel
QT_QPA_PLATFORM=offscreen \
ctest --test-dir build/zzpuretools-latest-release --output-on-failure
QT_QPA_PLATFORM=offscreen \
ctest --test-dir build/zzpuretools-latest-debug --output-on-failure
```

预期：Release 55/55 通过；Debug 55/55 通过（Debug 中 perf 测试按门控跳过）。

- [ ] **步骤 5：提交**

```bash
git add tests/perf/tst_PerfScrollback.cpp tests/perf/CMakeLists.txt
git commit -m "test(perf): 滚动历史门控切换到 ZzTermCore 桥链路"
```

## 任务 6：删除 ZzTermWidget 子模块与活跃残留

**文件：**
- 修改：`.gitmodules`
- 删除：`third_party/ZzTermWidget`（gitlink）
- 修改：`CMakeLists.txt`
- 修改：`README.md`
- 修改：`src/log/ZzLogLine.h`

- [ ] **步骤 1：确认活跃引用只剩元数据与说明**

```bash
git grep -n -E 'ZzTermWidget|QTermWidget|qtermwidget|ZzQTermWidget' -- \
  . ':!docs/superpowers' ':!docs/acceptance' ':!third_party'
```

预期：只剩 `.gitmodules`、根 `CMakeLists.txt` 注释、`README.md` 表格行、
`src/log/ZzLogLine.h` 注释；测试已在任务 4/5 适配。

- [ ] **步骤 2：清理过时说明**

删除根 `CMakeLists.txt` 中：

```cmake
# Z1：ZzTermWidget（qtermwidget fork）已摘除——终端前端由
# ZzCoreTerminalWidget（ZzTermCore 引擎）接管；子模块目录保留在
# third_party/ 供配色/键位资源取值对照，不再参与构建。
```

删除 `README.md` 子模块表中：

```markdown
| `ZzTermWidget` | 旧终端组件（qtermwidget fork）——Z1 起不参与构建，仅保留供配色/键位资源取值对照，Z4 删除 |
```

把 `src/log/ZzLogLine.h` 注释改为：

```cpp
/**
 * @brief 滚动历史中的一行：纯文本 + 不透明的字符属性负载。
 *
 * attributes 承载颜色 / 粗体等终端层字符属性；日志引擎只做不透明存储与
 * 回读，不解析内容，也不绑定任一终端前端实现。
 */
```

- [ ] **步骤 3：删除子模块和本机元数据**

```bash
git submodule deinit -f -- third_party/ZzTermWidget
git rm -f third_party/ZzTermWidget
git config --local --remove-section submodule.third_party/ZzTermWidget || true
rm -rf .git/modules/third_party/ZzTermWidget
```

检查 `.gitmodules` 中以下 section 已被 `git rm` 自动删除；若仍存在，手动删除：

```ini
[submodule "third_party/ZzTermWidget"]
	path = third_party/ZzTermWidget
	url = git@github.com:jackfahdin/ZzTermWidget.git
```

- [ ] **步骤 4：验证仓库状态与活跃引用**

```bash
git status --short
git submodule status
git ls-tree HEAD third_party/ZzTermWidget
git grep -n -E 'ZzTermWidget|QTermWidget|qtermwidget|ZzQTermWidget' -- \
  . ':!docs/superpowers' ':!docs/acceptance' ':!third_party'
```

预期：`git ls-tree` 无输出；活跃区域 grep 无输出；历史文档允许保留旧名称。

- [ ] **步骤 5：最终本机验证**

```bash
cmake --build build/zzpuretools-latest-debug --parallel
QT_QPA_PLATFORM=offscreen \
ctest --test-dir build/zzpuretools-latest-debug --output-on-failure
cmake --build build/zzpuretools-latest-release --parallel
QT_QPA_PLATFORM=offscreen \
ctest --test-dir build/zzpuretools-latest-release --output-on-failure
```

预期：Debug/Release 均 55/55 通过。

- [ ] **步骤 6：提交并推送**

```bash
git add .gitmodules CMakeLists.txt README.md src/log/ZzLogLine.h \
  third_party/ZzTermWidget
git commit -m "chore(third_party): 删除废弃 ZzTermWidget 子模块"
git push origin master
```

推送后由现有三平台 CI 做最终验证；不新增 workflow。

## 7. 计划自检

- 规格 §1 的两项目标分别由任务 1-3 和任务 4-6 覆盖。
- 规格 §4.1 的上游修复对应任务 1。
- 规格 §4.2 的 pin、Stacked、旧布局清理和标题 workaround 评估对应任务 2-3。
- 规格 §4.3 的 Segoe 风险不新增代码任务，按用户定案保留。
- 规格 §4.4 的四个测试残留分别由任务 4（三个单测）和任务 5（性能测试）覆盖。
- 规格 §4.5 的元数据删除与活跃注释清理对应任务 6。
- 规格 §6 的 Debug/Release、重点测试、Linux 构建与 CI 验证均有对应步骤。
- 计划中没有占位符；所有需要修改的文件均已点名。
