# Z2 显示特性收口 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 收口 ZzClawTerm 终端区 8 项显示特性（iTerm2 配色全量替换、GBK 激活、IME、滚动条+历史读回桥、右键菜单、缩放、最小网格、Alt 屏滚轮），每单元 QTest + 用户手测。

**架构：** 全部改动落在 `ZzCoreTerminalWidget`（Qt 前端）/ `ZzTerminalView`（胶水）/ `ZzScrollbackBridge`（历史桥）+ 新增 `ZzTerminalPalette`（配色方案表，零 Widget 依赖）；引擎侧缺口（历史最旧端注入原语）走 ZzTermCore M19 小里程碑前置，不在本计划改引擎。

**技术栈：** Qt6 Widgets/QTest（offscreen）、Qt6::Core5Compat、ZzTermCore 静态库（third_party 子模块）、Konsole .colorscheme 格式（iTerm2-Color-Schemes vendored 快照）。

**规格：** `docs/superpowers/specs/2026-10-12-z2-display-features-design.md`（决策记录 §2 为本计划定案依据）

**仓库/基线：** ZzClawTerm master @ 01db643；测试基线 50/50（`ctest` 按二进制注册，`zz_add_qtest(name unit/file.cpp)`，offscreen）。

**通用命令：**

```bash
# 配置+构建（已有 build 目录则跳过 cmake 首行）
cmake -S . -B build/linux-gcc-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-gcc-debug -j$(nproc)
# 全量测试
ctest --test-dir build/linux-gcc-debug --output-on-failure
# 单测
./build/linux-gcc-debug/tests/tst_<名>
```

---

## 文件结构

| 文件 | 职责 | 涉及任务 |
| --- | --- | --- |
| `src/terminal/colorschemes/`（新建，~340 个 .colorscheme + LICENSE + PROVENANCE.md + colorschemes.qrc） | iTerm2-Color-Schemes vendored 快照与 Qt 资源清单 | 1 |
| `src/terminal/ZzTerminalPalette.{h,cpp}`（新建） | 配色方案表：qrc 枚举 + INI 解析 + 字段级回退 + 256 色立方体 | 1 |
| `tests/unit/tst_ZzTerminalPalette.cpp`（新建） | palette 契约测试 | 1 |
| `src/terminal/ZzCoreTerminalWidget.{h,cpp}` | 渲染消费 palette、IME、右键菜单、缩放、最小网格、Alt 滚轮、滚动信号 | 2,4,5,6,7,8,9 |
| `src/terminal/ZzTerminalView.{h,cpp}` | 配色/字号应用、滚动条接线、GBK 解码（既有） | 2,5 |
| `src/terminal/ZzScrollbackBridge.{h,cpp}` | 读回注入桥 + reflow 对账复位 | 5 |
| `src/settings/ZzAppSettings.cpp` | availableColorSchemes 委托 palette、默认值 "Tango Dark" | 2 |
| `src/menu/ZzMenuBarService.cpp` | 精选名单换 iTerm2 方案名 | 2 |
| `CMakeLists.txt` / `src/CMakeLists.txt` | Core5Compat、qrc 入源 | 1,3 |
| `tests/unit/tst_ZzTerminalView.cpp` | 解 GBK QSKIP、配色接线断言 | 2,3 |
| `tests/unit/tst_ZzCoreTerminalWidget.cpp` | IME/菜单/缩放/网格/Alt 滚轮用例扩充 | 4,6,7,8,9 |
| `tests/unit/tst_ZzScrollbackBridge.cpp` | 读回注入 + reflow 复位用例扩充 | 5 |
| `tests/unit/tst_ZzAppSettings.cpp`、`tst_ZzMenuBarService.cpp`、`tst_ZzSettingsPage.cpp` | 默认值/清单断言更新 | 2 |

---

## 任务 1：iTerm2 配色 vendoring + ZzTerminalPalette 单元

**文件：**
- 创建：`src/terminal/colorschemes/`（*.colorscheme、`LICENSE`、`PROVENANCE.md`、`colorschemes.qrc`）
- 创建：`src/terminal/ZzTerminalPalette.h`、`src/terminal/ZzTerminalPalette.cpp`
- 创建：`tests/unit/tst_ZzTerminalPalette.cpp`
- 修改：`src/CMakeLists.txt`（qrc 入 `ZZCLAWTERM_APP_SOURCES`）、`tests/CMakeLists.txt`（注册新测试，在 :50 附近既有 `zz_add_qtest` 行旁追加）

Konsole .colorscheme 格式（INI，本任务只需这些节/键，未知节键忽略）：

```ini
[Background]
Color=31,31,45
[Foreground]
Color=204,204,204
[Color0]
Color=0,0,0
[Color0Intense]
Color=104,104,104
; … Color1..Color7 / Color1Intense..Color7Intense
```

- [ ] **步骤 1：vendoring 上游快照**

```bash
cd /tmp
git ls-remote https://github.com/mbadolato/iTerm2-Color-Schemes.git HEAD
# 记录输出哈希 → 写入 PROVENANCE.md
curl -L -o i2cs.tar.gz https://codeload.github.com/mbadolato/iTerm2-Color-Schemes/tar.gz/refs/heads/master
tar xzf i2cs.tar.gz
mkdir -p /home/zz/Jackfahdin/github/ZzClawTerm/src/terminal/colorschemes
cp iTerm2-Color-Schemes-master/konsole/*.colorscheme \
   /home/zz/Jackfahdin/github/ZzClawTerm/src/terminal/colorschemes/
cp iTerm2-Color-Schemes-master/LICENSE \
   /home/zz/Jackfahdin/github/ZzClawTerm/src/terminal/colorschemes/LICENSE
ls /home/zz/Jackfahdin/github/ZzClawTerm/src/terminal/colorschemes/*.colorscheme | wc -l
```

预期：文件数 > 250；确认存在 `Tango Dark.colorscheme`、`Solarized Dark.colorscheme`、`Dracula.colorscheme`（任务 2 精选名单依赖，实际清单以 vendored 文件名为准）。

- [ ] **步骤 2：写 PROVENANCE.md 与生成 colorschemes.qrc**

`src/terminal/colorschemes/PROVENANCE.md` 内容（哈希填步骤 1 实测值）：

```markdown
# 配色方案来源

- 上游：https://github.com/mbadolato/iTerm2-Color-Schemes
- 快照 commit：<步骤 1 的 HEAD 哈希>
- 快照日期：2026-10-12
- 取用路径：konsole/*.colorscheme（全量）+ LICENSE（MIT）
- 更新方式：重新执行 Z2 计划任务 1 步骤 1-2 并更新本文件哈希
```

生成 qrc（文件名可能含 `&` 等 XML 特殊字符，必须转义）：

```bash
cd /home/zz/Jackfahdin/github/ZzClawTerm/src/terminal/colorschemes
{
  printf '<RCC>\n    <qresource prefix="/colorschemes">\n'
  for f in *.colorscheme; do
    esc=$(printf '%s' "$f" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g')
    printf '        <file>%s</file>\n' "$esc"
  done
  printf '    </qresource>\n</RCC>\n'
} > colorschemes.qrc
```

把 qrc 入源：`src/CMakeLists.txt` 的 `ZZCLAWTERM_APP_SOURCES` 列表中追加一行
`terminal/colorschemes/colorschemes.qrc`（根 CMakeLists 已 `CMAKE_AUTORCC ON`，自动编译）。

- [ ] **步骤 3：编写失败的测试**

`tests/unit/tst_ZzTerminalPalette.cpp`：

```cpp
#include <QtTest/QtTest>

#include "terminal/ZzTerminalPalette.h"

/**
 * @brief ZzTerminalPalette（Z2 任务 1）契约：qrc 枚举全量方案、
 * INI 解析、未知名/缺字段回退默认方案、256 色索引解析。
 */
class tst_ZzTerminalPalette : public QObject
{
    Q_OBJECT
private slots:
    void enumeratesVendoredSchemes()
    {
        const QStringList all = ZzTerminalPalette::availableSchemes();
        QVERIFY(all.size() > 250);
        QVERIFY(all.contains(ZzTerminalPalette::defaultSchemeName()));
        // 字母序（设置页下拉框直接消费）
        const QStringList sorted = all;
        QVERIFY(all == sorted);
    }

    void parsesSchemeFields()
    {
        const ZzTerminalPalette::Scheme s =
            ZzTerminalPalette::scheme(QStringLiteral("Tango Dark"));
        QVERIFY(s.foreground.isValid());
        QVERIFY(s.background.isValid());
        QVERIFY(s.foreground != s.background);
        for (const QColor &c : s.ansi)
            QVERIFY(c.isValid());
    }

    void unknownNameFallsBackToDefault()
    {
        const ZzTerminalPalette::Scheme a =
            ZzTerminalPalette::scheme(QStringLiteral("NoSuchScheme-xyz"));
        const ZzTerminalPalette::Scheme b =
            ZzTerminalPalette::scheme(ZzTerminalPalette::defaultSchemeName());
        QCOMPARE(a.foreground, b.foreground);
        QCOMPARE(a.background, b.background);
        QCOMPARE(a.ansi[0], b.ansi[0]);
        QCOMPARE(a.ansi[15], b.ansi[15]);
    }

    void indexedColorCubeAndGray()
    {
        const ZzTerminalPalette::Scheme s =
            ZzTerminalPalette::scheme(ZzTerminalPalette::defaultSchemeName());
        // 0-15 走方案表
        QCOMPARE(ZzTerminalPalette::colorForIndex(s, 0), s.ansi[0]);
        QCOMPARE(ZzTerminalPalette::colorForIndex(s, 15), s.ansi[15]);
        // 16-231：xterm 立方体（v=0 → 0，否则 55+v*40）；索引 16 = (0,0,0)
        QCOMPARE(ZzTerminalPalette::colorForIndex(s, 16), QColor(0, 0, 0));
        QCOMPARE(ZzTerminalPalette::colorForIndex(s, 21), QColor(0, 0, 255));
        // 232-255：灰阶 8+i*10
        QCOMPARE(ZzTerminalPalette::colorForIndex(s, 232), QColor(8, 8, 8));
        QCOMPARE(ZzTerminalPalette::colorForIndex(s, 255), QColor(238, 238, 238));
        // 越界回退默认前景
        QCOMPARE(ZzTerminalPalette::colorForIndex(s, 256), s.foreground);
    }
};
QTEST_MAIN(tst_ZzTerminalPalette)
#include "tst_ZzTerminalPalette.moc"
```

`tests/CMakeLists.txt` 追加：

```cmake
zz_add_qtest(tst_ZzTerminalPalette unit/tst_ZzTerminalPalette.cpp)
```

- [ ] **步骤 4：运行测试验证失败**

运行：`cmake --build build/linux-gcc-debug -j$(nproc) && ./build/linux-gcc-debug/tests/tst_ZzTerminalPalette`
预期：编译失败，`ZzTerminalPalette.h` 不存在。

- [ ] **步骤 5：实现 ZzTerminalPalette**

`src/terminal/ZzTerminalPalette.h`：

```cpp
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

/**
 * @brief 终端配色方案表（Z2）：Konsole .colorscheme 解析 + 全量方案枚举。
 *
 * 数据来源：qrc:/colorschemes/（iTerm2-Color-Schemes vendored 快照，
 * 见 colorschemes/PROVENANCE.md）。零 Qt Widget 依赖，可独立 QTest。
 * 未知名/解析失败/字段缺失一律回退默认方案对应值，不抛错。
 */
class ZzTerminalPalette
{
public:
    struct Scheme {
        QColor ansi[16];     ///< 0-7 普通 + 8-15 亮（Color0Intense..Color7Intense）
        QColor foreground;   ///< 默认前景（引擎 ZzColor::Default 前景解析值）
        QColor background;   ///< 默认背景
    };

    /** @brief 全部可用方案名（字母序；恒含 defaultSchemeName()）。 */
    [[nodiscard]] static QStringList availableSchemes();

    /** @brief 默认方案名（settings.ini 缺省与未知名回退目标）。 */
    [[nodiscard]] static QString defaultSchemeName();

    /** @brief 按名取方案；未知名或解析失败回退默认方案。 */
    [[nodiscard]] static Scheme scheme(const QString &name);

    /**
     * @brief 引擎 Indexed 色解析：0-15 查方案表，16-231 xterm 立方体，
     *        232-255 灰阶，越界回退 scheme.foreground。
     */
    [[nodiscard]] static QColor colorForIndex(const Scheme &scheme, int idx);
};
```

`src/terminal/ZzTerminalPalette.cpp` 要点（完整实现）：

```cpp
#include "ZzTerminalPalette.h"

#include <QDir>
#include <QFile>
#include <QHash>

namespace {

struct RawScheme {
    QColor ansi[16];
    QColor foreground;
    QColor background;
    bool hasAnsi[16] = {};
    bool hasFg = false;
    bool hasBg = false;
};

[[nodiscard]] bool parseColor(const QString &text, QColor &out)
{
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != 3)
        return false;
    bool ok[3];
    const int r = parts[0].trimmed().toInt(&ok[0]);
    const int g = parts[1].trimmed().toInt(&ok[1]);
    const int b = parts[2].trimmed().toInt(&ok[2]);
    if (!ok[0] || !ok[1] || !ok[2])
        return false;
    out = QColor(r, g, b);
    return out.isValid();
}

/** @brief 解析单个 .colorscheme（qrc 路径）；整体读取失败返回 false。 */
[[nodiscard]] bool parseSchemeFile(const QString &name, RawScheme &out)
{
    QFile file(QStringLiteral(":/colorschemes/") + name
               + QStringLiteral(".colorscheme"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QString section;
    while (!file.atEnd()) {
        const QString line =
            QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';'))
            || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        if (line.left(eq) != QLatin1String("Color"))
            continue; // 只关心 Color= 键，其余（Blur 等）忽略
        QColor color;
        if (!parseColor(line.mid(eq + 1), color))
            continue;
        if (section == QLatin1String("Background")) {
            out.background = color;
            out.hasBg = true;
        } else if (section == QLatin1String("Foreground")) {
            out.foreground = color;
            out.hasFg = true;
        } else {
            // Color0..Color7 → ansi[0..7]；Color0Intense.. → ansi[8..15]
            int idx = -1;
            if (section.startsWith(QLatin1String("Color"))) {
                QString rest = section.mid(5);
                const bool intense = rest.endsWith(QLatin1String("Intense"));
                if (intense)
                    rest.chop(7);
                bool ok = false;
                const int n = rest.toInt(&ok);
                if (ok && n >= 0 && n <= 7)
                    idx = intense ? 8 + n : n;
            }
            if (idx >= 0) {
                out.ansi[idx] = color;
                out.hasAnsi[idx] = true;
            }
        }
    }
    return true;
}

/** @brief 方案缓存（字段级回退：缺失字段取默认方案对应值）。 */
[[nodiscard]] ZzTerminalPalette::Scheme loadScheme(const QString &name)
{
    static QHash<QString, ZzTerminalPalette::Scheme> cache;
    const auto it = cache.constFind(name);
    if (it != cache.constEnd())
        return it.value();

    ZzTerminalPalette::Scheme result;
    RawScheme raw;
    const bool ok = parseSchemeFile(name, raw);
    if (!ok && name != ZzTerminalPalette::defaultSchemeName())
        return loadScheme(ZzTerminalPalette::defaultSchemeName()); // 未知名整体回退
    if (!ok) {
        // 默认方案自身损坏（不应发生）：保底硬编码黑底白字
        result.foreground = QColor(0xcc, 0xcc, 0xcc);
        result.background = QColor(0x00, 0x00, 0x00);
        for (int i = 0; i < 16; ++i)
            result.ansi[i] = result.foreground;
        cache.insert(name, result);
        return result;
    }
    const ZzTerminalPalette::Scheme dflt =
        name == ZzTerminalPalette::defaultSchemeName()
            ? ZzTerminalPalette::Scheme{}
            : loadScheme(ZzTerminalPalette::defaultSchemeName());
    // 默认方案字段缺失时的最终保底（字段级回退链的最后一环）
    ZzTerminalPalette::Scheme fallback = dflt;
    if (name == ZzTerminalPalette::defaultSchemeName()) {
        fallback.foreground = QColor(0xcc, 0xcc, 0xcc);
        fallback.background = QColor(0x00, 0x00, 0x00);
        for (int i = 0; i < 16; ++i)
            fallback.ansi[i] = fallback.foreground;
    }
    result.foreground = raw.hasFg ? raw.foreground : fallback.foreground;
    result.background = raw.hasBg ? raw.background : fallback.background;
    for (int i = 0; i < 16; ++i)
        result.ansi[i] = raw.hasAnsi[i] ? raw.ansi[i] : fallback.ansi[i];
    cache.insert(name, result);
    return result;
}

} // namespace

QStringList ZzTerminalPalette::availableSchemes()
{
    QStringList out;
    const QDir dir(QStringLiteral(":/colorschemes"));
    const QStringList files = dir.entryList({QStringLiteral("*.colorscheme")},
                                            QDir::Files);
    out.reserve(files.size());
    for (const QString &f : files)
        out.append(f.left(f.size() - 12)); // 去 ".colorscheme" 后缀
    out.sort(Qt::CaseInsensitive);
    return out;
}

QString ZzTerminalPalette::defaultSchemeName()
{
    return QStringLiteral("Tango Dark");
}

ZzTerminalPalette::Scheme ZzTerminalPalette::scheme(const QString &name)
{
    return loadScheme(name);
}

QColor ZzTerminalPalette::colorForIndex(const Scheme &scheme, int idx)
{
    if (idx >= 0 && idx < 16)
        return scheme.ansi[idx];
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
    return scheme.foreground;
}
```

- [ ] **步骤 6：运行测试验证通过**

运行：`cmake --build build/linux-gcc-debug -j$(nproc) && ./build/linux-gcc-debug/tests/tst_ZzTerminalPalette`
预期：4 用例全 PASS。

- [ ] **步骤 7：全量回归 + Commit**

运行：`ctest --test-dir build/linux-gcc-debug --output-on-failure`
预期：51/51（50 基线 + 新增 1）。

```bash
git add src/terminal/colorschemes src/terminal/ZzTerminalPalette.h \
        src/terminal/ZzTerminalPalette.cpp src/CMakeLists.txt \
        tests/unit/tst_ZzTerminalPalette.cpp tests/CMakeLists.txt
git commit -m "feat(terminal): Z2 任务 1 ZzTerminalPalette + iTerm2 配色全量 vendoring"
```

---

## 任务 2：配色接线（渲染消费 + 四处消费点 + 默认值切换）

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.h`、`src/terminal/ZzCoreTerminalWidget.cpp`（:56-114 调色板区、:116 构造、:188 `setTerminalFontPointSize` 之后新增方法）
- 修改：`src/terminal/ZzTerminalView.cpp`（:178-201 `applySettings`）
- 修改：`src/settings/ZzAppSettings.cpp`（:60-91 默认值与 availableColorSchemes）
- 修改：`src/menu/ZzMenuBarService.cpp`（:24-28 kCuratedSchemes）
- 测试：`tests/unit/tst_ZzCoreTerminalWidget.cpp`、`tests/unit/tst_ZzTerminalView.cpp`、`tests/unit/tst_ZzAppSettings.cpp`、`tests/unit/tst_ZzMenuBarService.cpp`、`tests/unit/tst_ZzSettingsPage.cpp`

- [ ] **步骤 1：编写失败的测试**

`tests/unit/tst_ZzCoreTerminalWidget.cpp` 追加用例：

```cpp
    void colorSchemeAppliesToRendering()
    {
        ZzCoreTerminalWidget widget;
        widget.setColorScheme(QStringLiteral("Tango Dark"));
        // 方案切换后默认背景色变化即生效（paintEvent 全铺背景）
        widget.setColorScheme(QStringLiteral("Solarized Light"));
        // 不断言像素（offscreen 无意义），断言内部态与无崩溃：
        QCOMPARE(widget.colorSchemeName(), QStringLiteral("Solarized Light"));
    }

    void unknownSchemeFallsBack()
    {
        ZzCoreTerminalWidget widget;
        widget.setColorScheme(QStringLiteral("NoSuchScheme-xyz"));
        QCOMPARE(widget.colorSchemeName(),
                 ZzTerminalPalette::defaultSchemeName());
    }
```

（顶部 `#include "terminal/ZzTerminalPalette.h"`。）

`tests/unit/tst_ZzTerminalView.cpp` 追加：

```cpp
    void applySettingsPropagatesColorScheme()
    {
        ZzTerminalView view;
        ZzAppSettings settings(QStringLiteral("/tmp/zz-z2-test-settings.ini"));
        settings.setColorScheme(QStringLiteral("Dracula"));
        view.applySettings(settings);
        QCOMPARE(view.termWidget()->colorSchemeName(), QStringLiteral("Dracula"));
    }
```

（顶部 include `settings/ZzAppSettings.h`；注意测试结束删除该 ini 或用 QDir::temp 唯一名。）

- [ ] **步骤 2：运行验证失败**

预期：编译失败，`setColorScheme`/`colorSchemeName` 不是成员。

- [ ] **步骤 3：ZzCoreTerminalWidget 消费 palette**

`ZzCoreTerminalWidget.h` 新增（public 区，`setTerminalFontPointSize` 声明之后）：

```cpp
    /** @brief 切换配色方案（未知名回退默认）；即时生效（重绘）。 */
    void setColorScheme(const QString &name);
    /** @brief 当前方案名（已回退后的实际值）。 */
    [[nodiscard]] QString colorSchemeName() const noexcept { return schemeName_; }
```

private 区新增：

```cpp
    /** @brief 引擎 ZzColor 经当前方案解析为 QColor（替代 Z1 自由函数）。 */
    [[nodiscard]] QColor resolveCellColor(ZzColor color, bool isForeground) const;
    ZzTerminalPalette::Scheme scheme_;   ///< 当前方案（构造即默认方案）
    QString schemeName_;                 ///< 当前方案名（回退后实际值）
```

`ZzCoreTerminalWidget.cpp`：

1. 删除匿名命名空间里的 `kDefaultPalette` 与自由函数 `resolveCellColor`（:56-114）。
2. 构造函数 `term_(...)` 初始化后追加：

```cpp
    scheme_     = ZzTerminalPalette::scheme(ZzTerminalPalette::defaultSchemeName());
    schemeName_ = ZzTerminalPalette::defaultSchemeName();
```

3. 新成员函数：

```cpp
QColor ZzCoreTerminalWidget::resolveCellColor(ZzColor color, bool isForeground) const
{
    switch (color.kind()) {
    case ZzColor::Kind::Rgb:
        return QColor(color.red(), color.green(), color.blue());
    case ZzColor::Kind::Indexed:
        return ZzTerminalPalette::colorForIndex(scheme_, color.index());
    default: // Default：对齐当前方案默认前景/背景（修 Z1 0xcc 与方案不一致）
        return isForeground ? scheme_.foreground : scheme_.background;
    }
}

void ZzCoreTerminalWidget::setColorScheme(const QString &name)
{
    const QString effective = ZzTerminalPalette::availableSchemes().contains(name)
                                  ? name
                                  : ZzTerminalPalette::defaultSchemeName();
    if (schemeName_ == effective)
        return;
    schemeName_ = effective;
    scheme_     = ZzTerminalPalette::scheme(effective);
    update();
}
```

4. `paintEvent` 与各绘制处的 `resolveCellColor(...)` 调用改为成员调用（删匿名命名空间后自然解析到成员）；`#include "ZzTerminalPalette.h"`。

注意：`ZzTerminalPalette::availableSchemes()` 每次切换会枚举 qrc，量大但切换低频；若 Profile 显示昂贵可在 palette 内加缓存（本计划不预判）。

- [ ] **步骤 4：ZzTerminalView::applySettings 接线配色**

`ZzTerminalView.cpp` :197-200 的占位注释与 `(void)settings.colorScheme();` 替换为：

```cpp
    m_term->setColorScheme(settings.colorScheme());
    // historyLines 运行时调整仍不生效（Core 构造期容量），保留读存。
    (void)settings.historyLines();
```

头文件注释（ZzTerminalView.h :56-57）同步更新：「配色与内存历史行数 Z1 读存暂不生效」改为「historyLines 读存暂不生效（Core 构造期容量）」。

- [ ] **步骤 5：ZzAppSettings 委托 + 默认值切换**

`ZzAppSettings.cpp`：

1. `availableColorSchemes()` 整体替换为：

```cpp
QStringList ZzAppSettings::availableColorSchemes()
{
    return ZzTerminalPalette::availableSchemes(); // Z2：iTerm2 全量方案表
}
```

（顶部 `#include "terminal/ZzTerminalPalette.h"`。）

2. `colorScheme()` getter 默认值 `"Linux"` 改为 `QStringLiteral("Tango Dark")`（找到 `terminal/colorScheme` 的 `m_settings->value(...)` 行）。

3. `ZzAppSettings.h` :38 与 :42-47 注释更新：去掉「Z1 过渡」说明，改为「Z2：委托 ZzTerminalPalette（iTerm2-Color-Schemes vendored）」。

- [ ] **步骤 6：菜单精选名单换 iTerm2 方案名**

`ZzMenuBarService.cpp` :24-28 `kCuratedSchemes` 替换为（执行时先 `ls src/terminal/colorschemes/` 核实每个名字存在，不存在的用 vendored 实际名替换）：

```cpp
const char *kCuratedSchemes[] = {
    "Tango Dark", "Solarized Dark", "Solarized Light", "Dracula",
    "Nord", "Tokyo Night", "Gruvbox Dark", "OneHalfDark",
    "Catppuccin Mocha", "Ubuntu",
};
```

`curatedColorSchemes()` 过滤逻辑（:58-71）不动——不存在的条目 warn+skip，正好是名单与实际 vendored 差异的容错。

- [ ] **步骤 7：更新受影响的既有测试**

运行全量测试找出红掉的断言，逐一更新为新口径：

```bash
ctest --test-dir build/linux-gcc-debug --output-on-failure 2>&1 | grep -E "FAIL|PASS.*color" 
```

已知必改（逐一核对实际断言后改）：
- `tst_ZzAppSettings.cpp`：`colorScheme()` 默认值断言 `"Linux"` → `"Tango Dark"`；`availableColorSchemes()` 若断言固定清单 → 改为 `QVERIFY(list.size() > 250); QVERIFY(list.contains("Tango Dark"));`
- `tst_ZzMenuBarService.cpp`：精选名单断言 → 断言「精选 ⊆ availableColorSchemes() 且 size >= 8」
- `tst_ZzSettingsPage.cpp` :94：`availableColorSchemes().count()` 比较 → 同上口径
- `tst_ZzSshConfigPage.cpp`：若有同类断言同口径更新

- [ ] **步骤 8：全量回归 + Commit**

预期：52/52。

```bash
git add src/terminal src/settings src/menu tests
git commit -m "feat(terminal): Z2 任务 2 配色接线——渲染消费 palette、默认值 Tango Dark、菜单精选换 iTerm2 方案"
```

- [ ] **步骤 9：用户手测确认**

构建主程序，手测：设置页切换 3 个方案（含一亮色）即时生效；菜单主题子菜单切换生效；重启后方案保持。用户确认后进入任务 3。

---

## 任务 3：Qt5Compat 引入 + GBK 激活

**文件：**
- 修改：`CMakeLists.txt`（:16 find_package 行）
- 修改：`src/CMakeLists.txt`（:87-89 链接列表）
- 测试：`tests/unit/tst_ZzTerminalView.cpp`（:55-65 QSKIP 区）

- [ ] **步骤 1：编写/恢复失败的测试**

`tests/unit/tst_ZzTerminalView.cpp` :55-65 区域：删除 QSKIP 分支与「需 Qt5Compat」注释，恢复字节级断言为无条件执行（保留编码名读存断言）。即：构造 GBK 编码的 settings → applySettings → 断言 `encoding()` == "GBK"，并经 termWidget 的 sendData 链路断言 UTF-8「中」被编码为 GBK 字节 `0xD6 0xD0`（若既有 QSKIP 分支下已有该断言体，直接解除条件跳过）。

- [ ] **步骤 2：运行验证失败**

预期：FAIL——无 Core5Compat 时 `encodingForName("GBK")` 返回 nullopt，`applySettings` 回退 UTF-8，编码结果不是 GBK 字节。

- [ ] **步骤 3：引入 Core5Compat**

`CMakeLists.txt` :16 行改为：

```cmake
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Test Xml Multimedia Core5Compat)
```

`src/CMakeLists.txt` 链接列表（:87-89）追加一行 `Qt6::Core5Compat`。

`ZzTerminalView.cpp` :189-191 注释更新：删「Qt6 无 Qt5Compat 时…Z2 立项」说明，改为「无效编码名（拼写错误等）回退 UTF-8 保底」。

- [ ] **步骤 4：运行验证通过 + 全量回归 + Commit**

预期：该测试 PASS，全量 52/52。

```bash
git add CMakeLists.txt src/CMakeLists.txt src/terminal/ZzTerminalView.cpp tests/unit/tst_ZzTerminalView.cpp
git commit -m "feat(terminal): Z2 任务 3 引入 Qt5Compat，GBK 等东亚编码激活"
```

- [ ] **步骤 5：用户手测确认**

GBK 会话（SSH 配置页选 GBK）收发中文不乱码。Windows/macOS CI 若缺 Core5Compat 组件属环境问题，报用户裁决（不自行降级方案）。

---

## 任务 4：IME preedit（中文输入）

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.h`（事件重写声明 + 成员）、`src/terminal/ZzCoreTerminalWidget.cpp`（:116 构造、:222 paintEvent、:411 keyPressEvent 附近）
- 测试：`tests/unit/tst_ZzCoreTerminalWidget.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
    void imeCommitSendsText()
    {
        ZzCoreTerminalWidget widget;
        QSignalSpy spy(&widget, &ZzCoreTerminalWidget::sendData);
        QInputMethodEvent commit(QString(), QList<QInputMethodEvent::Attribute>());
        commit.setCommitString(QStringLiteral("中文"));
        QApplication::sendEvent(&widget, &commit);
        QVERIFY(spy.count() >= 1);
        QByteArray all;
        for (const auto &call : spy)
            all += call.at(0).toByteArray();
        QCOMPARE(all, QStringLiteral("中文").toUtf8());
    }

    void imePreeditTrackedNotSent()
    {
        ZzCoreTerminalWidget widget;
        QSignalSpy spy(&widget, &ZzCoreTerminalWidget::sendData);
        QInputMethodEvent preedit(QStringLiteral("zhong"),
                                  QList<QInputMethodEvent::Attribute>());
        QApplication::sendEvent(&widget, &preedit);
        QCOMPARE(widget.preeditTextForTest(), QStringLiteral("zhong"));
        QCOMPARE(spy.count(), 0); // preedit 不进会话
    }
```

（顶部 include `<QInputMethodEvent>`、`<QApplication>`。）

- [ ] **步骤 2：运行验证失败**

预期：编译失败（`preeditTextForTest` 不存在）。

- [ ] **步骤 3：实现 IME 支持**

`ZzCoreTerminalWidget.h`：

- protected 区追加：`void inputMethodEvent(QInputMethodEvent *event) override;`
- public 区追加：`[[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;`（QWidget 是 public virtual）与 `[[nodiscard]] QString preeditTextForTest() const { return preedit_; }`
- private 成员追加：`QString preedit_; ///< IME 组合中串（绘制于光标处，不进会话）`
- 前置声明 `class QInputMethodEvent;`

`ZzCoreTerminalWidget.cpp`：

1. 构造函数追加 `setAttribute(Qt::WA_InputMethodEnabled, true);`
2. 实现：

```cpp
void ZzCoreTerminalWidget::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty()) {
        term_.sendText(event->commitString().toUtf8().toStdString());
        preedit_.clear();
    } else {
        preedit_ = event->preeditString();
    }
    update();
    event->accept();
}

QVariant ZzCoreTerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle: {
        // 光标微焦点矩形（输入法候选窗定位）；滚动回看时上报现行光标格即可
        const ZzCursorState cursor = term_.cursor();
        return QRect(cursor.position.col * cellWidth_,
                     cursor.position.row * cellHeight_,
                     cellWidth_, cellHeight_);
    }
    case Qt::ImFont:
        return font();
    default:
        return QWidget::inputMethodQuery(query);
    }
}
```

3. preedit 绘制：`paintWrapped` 尾部（光标绘制之后）追加——仅物理模式且 `scrollOffset_ == 0` 且光标可见时：

```cpp
    if (!preedit_.isEmpty()) {
        const ZzCursorState cursor = term_.cursor();
        if (cursor.visible && cursor.position.row < view.size().rows) {
            QFont f = painter.font();
            f.setUnderline(true); // preedit 惯例样式：下划线
            painter.setFont(f);
            painter.setPen(scheme_.foreground);
            const int y = cursor.position.row * cellHeight_ + cellAscent_;
            // 从光标格起绘，超出右缘部分裁剪（不换行模式由横滚覆盖，不特殊处理）
            painter.setClipRect(rect());
            painter.drawText(cursor.position.col * cellWidth_, y, preedit_);
            painter.setClipping(false);
        }
    }
```

（`paintUnwrapped` 暂不支持 preedit 叠加——YAGNI 登记，不换行模式属诊断向功能。）

- [ ] **步骤 4：运行验证通过 + 全量回归 + Commit**

预期：52/52（两新用例入既有测试二进制，不新增注册）。

```bash
git add src/terminal/ZzCoreTerminalWidget.h src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "feat(terminal): Z2 任务 4 IME preedit 与 commit 输入"
```

- [ ] **步骤 5：用户手测确认**

本地与 SSH 会话中：中文输入法打字可见 preedit（下划线）、上屏正确；vim 内中文输入正常。

---

## 任务 5：滚动条接线 + 历史读回注入桥 + reflow 对账复位

**前置门槛（先做，不过不进 SDD 子任务）——ZzTermCore M19 原语核查：**

```bash
grep -rn "prependOldest\|prependHistory" /home/zz/Jackfahdin/github/ZzClawTerm/third_party/ZzTermCore/include/
```

- 若命中：子模块 pin 已含原语，核对签名后直接开始步骤 1。
- 若未命中（预期）：**停止本任务，先走 ZzTermCore M19 小里程碑**（完整规格→计划→SDD 惯例），其验收标准：
  1. `ZzTerminal` 新增公共方法（建议名 `prependHistoryLines(std::vector<ZzLine> lines)`，语义=向 Primary 历史**最旧端**注入行；Alternate 期间拒绝并返回 false）；
  2. **注入 N 行使 `droppedLineCount()` 同步 +N**——HistoryView 绝对行号（dropped+idx）对既有行保持稳定，追加差分/选区锚点零扰动；注入行占绝对行号低位（被裁行的旧号段，内容本已落盘，不会被重追加）；
  3. 注入行计入容量：后续 append 超限裁剪先裁注入行（无害，LogEngine 是持久权威）；
  4. 行宽不变量：调用方以当前列宽供行（与 `ZzScrollback::append` 同一约定）；
  5. Core 单测覆盖：注入后 lineAt 顺序、dropped 平移、reflow 后注入行重组。
  
  M19 收官（tag + CI 绿）后 bump ZzClawTerm 子模块 pin 再继续。

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.h`、`src/terminal/ZzCoreTerminalWidget.cpp`（新增信号/方法）
- 修改：`src/terminal/ZzTerminalView.h`、`src/terminal/ZzTerminalView.cpp`（:20-79 构造布局）
- 修改：`src/terminal/ZzScrollbackBridge.h`、`src/terminal/ZzScrollbackBridge.cpp`
- 测试：`tests/unit/tst_ZzScrollbackBridge.cpp`、`tests/unit/tst_ZzCoreTerminalWidget.cpp`

**设计要点（规格 §4.4 落地）：**

- 视口越顶触发：widget 在「上滚且已到历史顶」时发射 `historyTopReached()`；桥收到后注入一块（200 行）。
- 注入后视口稳定：注入只增长历史最旧端，scrollOffset_（离底行数）数值不变即视口不动；用户继续上滚自然进入新注入区。
- reflow 对账复位：contentFed 差分中检测 `newest < m_seenLineNo`（reflow/重组使行身份重建），游标回锚 `m_seenLineNo = newest`（内容已按旧形态落盘，不重复追加）。
- 注入行不重追加：M19 语义（dropped+N）保证注入行绝对行号低于 m_seenLineNo，既有差分天然跳过。

- [ ] **步骤 1：编写失败的测试（桥）**

`tests/unit/tst_ZzScrollbackBridge.cpp` 追加（沿用该文件既有引擎构造脚手架，读它先）：

```cpp
    void scrollTopInjectsOlderLines()
    {
        // 脚手架：造引擎 + 桥（参照既有用例）；先经 widget 喂 50 行触发落盘
        // …（setup 与既有用例一致）…
        ZzCoreTerminalWidget &widget = /* 桥绑定的 widget */;
        // 把 Core 内存历史缩到 10 行便于测试：逐行 feed 超容量（或直接用
        // 小容量 widget——若 widget 构造固定 10000 行，则改为向引擎
        // appendLine 预置 30 行「更早历史」模拟冷数据，Core 内只有 20 行）
        // 触发越顶：
        emit widget.historyTopReached(); // 或直接调桥注入槽（若为 public）
        // 断言：Core 历史行数增长（注入行进入最旧端）
        QVERIFY(widget.terminal().historyView().lineCount() > 注入前);
        // 断言：读回行内容 = 引擎中更早的行（纯文本、无样式）
    }

    void reflowResetsAppendCursor()
    {
        // feed 若干行落盘 → 记录 m_seenLineNo 等价物（经 totalLines 观察）
        // resize 变列触发 reflow（generation 变、行数重组）
        // 再 feed 新行 → 断言引擎只追加新行，不重复追加重组前的历史
    }
```

执行者注意：上述注释行是搭建指引，实现时落成具体代码（参照文件内既有 `appendLine`/`getLines` 用法）；若 widget 的 10000 行容量使「喂满再溢出」不现实，用「引擎预置冷行 + Core 短历史」的非对称构造。

- [ ] **步骤 2：编写失败的测试（widget 信号）**

`tests/unit/tst_ZzCoreTerminalWidget.cpp` 追加：

```cpp
    void wheelAtHistoryTopEmitsTopReached()
    {
        ZzCoreTerminalWidget widget;
        widget.show();
        for (int i = 0; i < 30; ++i)
            widget.recvData(QByteArray("line\r\n"));
        QSignalSpy spy(&widget, &ZzCoreTerminalWidget::historyTopReached);
        widget.setScrollOffset(widget.maxScrollOffset()); // 到顶
        QWheelEvent up(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0),
                       QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(&widget, &up);
        QVERIFY(spy.count() >= 1);
    }
```

- [ ] **步骤 3：运行验证失败**

预期：编译失败（`historyTopReached` 信号不存在）。

- [ ] **步骤 4：widget 侧实现**

`ZzCoreTerminalWidget.h`：

```cpp
signals:
    // …既有信号后追加…
    /** @brief 用户上滚已到历史最顶端（读回桥注入触发点）。 */
    void historyTopReached();

public:
    /** @brief 读回桥注入完成后调用：重发射滚动范围/夹取偏移/重绘。 */
    void notifyHistoryPrepended();
```

`wheelEvent`（:612-626）上滚分支前追加：

```cpp
    if (steps > 0 && scrollOffset_ >= maxScrollOffset() && maxScrollOffset() > 0) {
        emit historyTopReached(); // 已到顶仍上滚：通知桥注入更早历史
    }
```

新增方法：

```cpp
void ZzCoreTerminalWidget::notifyHistoryPrepended()
{
    emit scrollRangeChanged(maxScrollOffset()); // 上限增长，滚动条扩展
    setScrollOffset(scrollOffset_);             // 夹取（数值不变即视口稳定）
    update();
}
```

- [ ] **步骤 5：桥侧实现（ZzScrollbackBridge）**

`ZzScrollbackBridge.h` 追加：

```cpp
public:
    /** @brief 从 LogEngine 读回一块更早历史注入 Core 最旧端（默认 200 行）。 */
    void injectOlderBlock(int maxLines = 200);

private:
    qint64 m_injectedCount = 0; ///< 已注入 Core 最旧端的行数（引擎行号低段）
    std::uint64_t m_lastGeneration = 0; ///< 上一帧历史代计数（reflow 侦测）
```

`ZzScrollbackBridge.cpp`：

1. 构造内追加接线：

```cpp
    connect(m_term, &ZzCoreTerminalWidget::historyTopReached, this,
            [this]() { injectOlderBlock(); });
```

2. contentFed 差分 lambda 内（:16-38），`newest` 计算后、while 循环前插入 reflow 复位：

```cpp
        const std::uint64_t gen = hv.generation();
        if (gen != m_lastGeneration && newest < m_seenLineNo)
            // reflow/重组使行身份重建（dropped 不计重组）：游标回锚，
            // 已落盘内容按旧形态保留，不重复追加（规格 §4.4 对账复位）
            m_seenLineNo = newest;
        m_lastGeneration = gen;
```

3. 新增注入方法：

```cpp
void ZzScrollbackBridge::injectOlderBlock(int maxLines)
{
    const auto &hv = m_term->terminal().historyView();
    // Core 历史最旧行的引擎绝对行号（M19 语义：注入行使 dropped +N，
    // 故 Core 最旧端引擎行号 = dropped - 已注入数）
    const qint64 oldestInCore =
        qint64(hv.droppedLineCount()) - m_injectedCount;
    const QStringList older = readOlderLines(oldestInCore, maxLines);
    if (older.isEmpty())
        return; // 无更早历史：静默停在越顶处（规格 §5）

    const int cols = m_term->terminal().size().cols;
    std::vector<ZzLine> lines;
    lines.reserve(std::size_t(older.size()));
    for (const QString &text : older) {
        ZzLine line(cols);
        int col = 0;
        for (const QChar ch : text) { // 注：BMP 外字符按代理对逐码位填入
            if (col >= cols)
                break; // 读回行超宽截断（纯文本口径，丢样式为既有口径）
            ZzCell cell;
            cell.setCodePoint(ch.unicode());
            // CJK 宽字符粗判（与引擎 wcwidth 全表有差距，读回显示可接受）
            const ushort u = ch.unicode();
            const bool wide = u >= 0x1100 && (u <= 0x115F || u >= 0x2E80);
            cell.setWidth(wide ? ZzCellWidth::Wide : ZzCellWidth::Narrow);
            line.setCell(col++, cell);
            if (wide && col < cols)
                ++col; // 宽字符占两格，续格留空
        }
        lines.push_back(std::move(line));
    }
    // M19 原语（ZzTerminal::prependHistoryLines）：注入历史最旧端，
    // droppedLineCount 同步 +N，绝对行号体系对既有行零扰动
    if (m_term->terminal().prependHistoryLines(std::move(lines))) {
        m_injectedCount += older.size();
        m_term->notifyHistoryPrepended();
    }
}
```

（头文件顶部 include `<ZzTerm/Line.h>`；若 M19 落地签名不同，以 Core 实际头文件为准并同步本计划勘误。）

- [ ] **步骤 6：ZzTerminalView 滚动条接线**

`ZzTerminalView.cpp` 构造函数：布局从 `layout->addWidget(m_term, 1)`（:53）改为网格布局：

```cpp
    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    m_vbar = new QScrollBar(Qt::Vertical, this);
    m_hbar = new QScrollBar(Qt::Horizontal, this);
    m_hbar->hide(); // 仅不换行模式出现
    grid->addWidget(m_term, 0, 0);
    grid->addWidget(m_vbar, 0, 1);
    grid->addWidget(m_hbar, 1, 0);
    layout->addLayout(grid, 1);

    // 滚动条 ⇄ widget 双向接线（值域即 scrollOffset：0=底部最新）
    connect(m_term, &ZzCoreTerminalWidget::scrollRangeChanged,
            m_vbar, &QScrollBar::setMaximum);
    connect(m_term, &ZzCoreTerminalWidget::scrollOffsetChanged,
            m_vbar, &QScrollBar::setValue);
    connect(m_vbar, &QScrollBar::valueChanged, this, [this](int value) {
        if (value == m_vbar->maximum() && value > 0)
            emit m_term->historyTopReached(); // 拖到顶同样触发读回注入
        m_term->setScrollOffset(value);
    });
    connect(m_term, &ZzCoreTerminalWidget::hScrollRangeChanged, this,
            [this](int max) {
                m_hbar->setMaximum(max);
                m_hbar->setVisible(max > 0);
            });
    connect(m_term, &ZzCoreTerminalWidget::hScrollOffsetChanged,
            m_hbar, &QScrollBar::setValue);
    connect(m_hbar, &QScrollBar::valueChanged,
            m_term, &ZzCoreTerminalWidget::setHScrollOffset);
```

（顶部 include `<QtWidgets/QGridLayout>`、`<QtWidgets/QScrollBar>`；`ZzTerminalView.h` private 追加 `QScrollBar *m_vbar = nullptr; QScrollBar *m_hbar = nullptr;` 与前置声明。同时删除 :25-26 的「Z1 暂缺滚动条」注释中滚动条部分——最小网格约束仍属任务 8。）

注意 `emit m_term->historyTopReached()` 从外部发射信号在 Qt 允许（信号即 public 成员函数语义的变体在 Qt6 需信号为 public——本计划信号区本就是 public 语义段，若编译受阻改为在 widget 上加 public 触发方法 `requestOlderHistory()` 由 view 调用）。

- [ ] **步骤 7：运行验证通过 + 全量回归 + Commit**

预期：54/54（桥测试扩充视拆分粒度，以实际注册为准）。

```bash
git add src/terminal tests
git commit -m "feat(terminal): Z2 任务 5 滚动条接线 + 历史读回注入桥 + reflow 对账复位"
```

- [ ] **步骤 8：用户手测确认**

长会话（historyLines 以上输出）：滚动条/滚轮到顶继续上滚，能看到 Core 内存历史之外、从日志读回的更早内容（纯文本无颜色为预期）；resize 变列后历史不错位、不重行；鼠标滚轮上滚越顶多滚几次逐块加载。

---

## 任务 6：选区回归固化 + 右键菜单四件套

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.h`、`src/terminal/ZzCoreTerminalWidget.cpp`（:668-724 鼠标事件区、新增 contextMenuEvent）
- 测试：`tests/unit/tst_ZzCoreTerminalWidget.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
    void contextMenuCopyEnabledOnlyWithSelection()
    {
        ZzCoreTerminalWidget widget;
        widget.recvData(QByteArray("abcdef\r\n"));
        QVERIFY(!widget.hasSelectionForMenu()); // 无选区：复制不可用
        widget.terminal().setSelection({0, 0}, {0, 3});
        QVERIFY(widget.hasSelectionForMenu());
    }

    void selectAllCoversWholeContent()
    {
        ZzCoreTerminalWidget widget;
        widget.recvData(QByteArray("abc\r\ndef\r\n"));
        widget.selectAll();
        QCOMPARE(QString::fromStdString(widget.terminal().selectedText()),
                 QString("abc\ndef\n")); // 行尾空格修剪口径以引擎 selectedText 为准
    }

    void unwrapLongLineFullCopyRegression()
    {
        // 回归 spike 三案（锚点重放/画笔外泄/端点+1）：不换行模式选中
        // 超宽行，复制内容含屏外部分
        ZzCoreTerminalWidget widget;
        widget.show();
        widget.resize(10 * widget.cellWidthForTest(), 5 * widget.cellHeightForTest());
        widget.recvData(QByteArray("0123456789ABCDEFGHIJ\r\n")); // 20 列超宽
        widget.setUnwrapMode(true);
        widget.terminal().setSelection({0, 0}, {0, 20});
        QCOMPARE(QString::fromStdString(widget.terminal().selectedText()),
                 QString("0123456789ABCDEFGHIJ"));
    }
```

- [ ] **步骤 2：运行验证失败**

预期：编译失败（`hasSelectionForMenu`/`selectAll` 不存在）。

- [ ] **步骤 3：实现**

`ZzCoreTerminalWidget.h` public 追加：

```cpp
    /** @brief 全选（逻辑行空间 [0,0) → [lineCount,0) 半开区间）。 */
    void selectAll();
    /** @brief 复制当前选区到剪贴板（右键菜单与鼠标释放共用）。 */
    void copySelectionToClipboard();
    /** @brief 是否存在非空选区（菜单项可用性）。 */
    [[nodiscard]] bool hasSelectionForMenu() const;
```

protected 追加：`void contextMenuEvent(QContextMenuEvent *event) override;`

`ZzCoreTerminalWidget.cpp`：

```cpp
void ZzCoreTerminalWidget::selectAll()
{
    const auto total = (std::int64_t)term_.unwrapView().lineCount();
    term_.setSelection({0, 0}, {total, 0}); // 逻辑行空间全量半开区间
    update();
}

bool ZzCoreTerminalWidget::hasSelectionForMenu() const
{
    return term_.hasSelection();
}

void ZzCoreTerminalWidget::copySelectionToClipboard()
{
    if (!term_.hasSelection())
        return;
    const std::string text = term_.selectedText();
    if (!text.empty())
        QGuiApplication::clipboard()->setText(QString::fromStdString(text));
}

void ZzCoreTerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copy = menu.addAction(tr("复制"));
    copy->setEnabled(hasSelectionForMenu());
    QAction *paste = menu.addAction(tr("粘贴"));
    paste->setEnabled(!QGuiApplication::clipboard()->text().isEmpty());
    QAction *all = menu.addAction(tr("全选"));
    menu.addSeparator();
    QAction *clear = menu.addAction(tr("清空屏幕"));
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == copy) {
        copySelectionToClipboard();
    } else if (chosen == paste) {
        term_.sendText(QGuiApplication::clipboard()->text().toUtf8().toStdString());
    } else if (chosen == all) {
        selectAll();
    } else if (chosen == clear) {
        // 本地清屏（ED3+ED2+Home）：shell 不感知，与 fork 时代 Clear 同口径
        recvData(QByteArray("\x1b[3J\x1b[2J\x1b[H"));
    }
}
```

`mouseReleaseEvent`（:710-724）内联复制逻辑替换为 `copySelectionToClipboard();` 调用。`#include <QMenu>`、`<QContextMenuEvent>`。

- [ ] **步骤 4：运行验证通过 + 全量回归 + Commit**

```bash
git add src/terminal/ZzCoreTerminalWidget.h src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "feat(terminal): Z2 任务 6 右键菜单四件套 + 选区回归固化"
```

- [ ] **步骤 5：用户手测确认**

右键：无选区时复制灰掉；复制/粘贴/全选/清屏各试一次；不换行模式超宽行右键复制含屏外内容。

---

## 任务 7：Ctrl+滚轮缩放 + 隐藏标签页字号滞留修复

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.h`、`src/terminal/ZzCoreTerminalWidget.cpp`（:188-220 字号、:612-626 wheelEvent、新增 showEvent）
- 测试：`tests/unit/tst_ZzCoreTerminalWidget.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
    void ctrlWheelZoomsWithClamp()
    {
        ZzCoreTerminalWidget widget;
        widget.setTerminalFontPointSize(12);
        const auto wheelUp = [](QWidget *w) {
            QWheelEvent e(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0),
                          QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                          Qt::NoScrollPhase, false);
            QApplication::sendEvent(w, &e);
        };
        wheelUp(&widget);
        QCOMPARE(widget.font().pointSize(), 13);
        widget.setTerminalFontPointSize(6);
        QWheelEvent down(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0),
                         QPoint(0, -120), Qt::NoButton, Qt::ControlModifier,
                         Qt::NoScrollPhase, false);
        QApplication::sendEvent(&widget, &down);
        QCOMPARE(widget.font().pointSize(), 6); // 下限钳制
    }

    void hiddenTabFontSizeResyncsOnShow()
    {
        ZzCoreTerminalWidget widget;
        widget.show();
        widget.resize(100 * widget.cellWidthForTest(),
                      30 * widget.cellHeightForTest());
        widget.hide();
        widget.setTerminalFontPointSize(20); // 隐藏期不重算（既有口径）
        widget.show();                       // 重新可见必须补重算
        const int expectCols = widget.size().width() / widget.cellWidthForTest();
        QCOMPARE(widget.gridCols(), expectCols);
    }
```

- [ ] **步骤 2：运行验证失败**

预期：`ctrlWheelZoomsWithClamp` FAIL（Ctrl+滚轮当前走滚动分支）；`hiddenTabFontSizeResyncsOnShow` FAIL（show 后无 resizeEvent 时网格滞留）。

- [ ] **步骤 3：实现**

1. `wheelEvent` 首行插入 Ctrl 分支（在 Alternate 判断之前）：

```cpp
    if (event->modifiers() & Qt::ControlModifier) {
        const int steps = event->angleDelta().y() / 120;
        if (steps != 0) {
            // Ctrl+滚轮缩放：6-72pt 钳制；设置页改动经 applySettings 重置缩放
            const int next = qBound(6, font().pointSize() + steps, 72);
            if (next != font().pointSize())
                setTerminalFontPointSize(next);
            event->accept();
            return;
        }
    }
```

2. 字号滞留修复：`ZzCoreTerminalWidget.h` protected 追加 `void showEvent(QShowEvent *event) override;`；实现：

```cpp
void ZzCoreTerminalWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 隐藏期字号变更不重算网格（size() 是占位尺寸）；重新可见时若 Qt 不
    // 投递 resizeEvent（尺寸未变），网格会滞留旧 cell 口径——这里以现行
    // cell 尺寸对现行控件尺寸补一次对账（幂等：口径一致时 resize 早退）。
    const int cols = qMax(1, size().width() / cellWidth_);
    const int rows = qMax(1, size().height() / cellHeight_);
    const ZzSize current = term_.size();
    if (cols != current.cols || rows != current.rows) {
        const int maxHBefore = maxHScrollOffset();
        if (term_.resize(cols, rows)) {
            traceResize(cols, rows);
            emit gridResized(cols, rows);
            emit scrollRangeChanged(maxScrollOffset());
            if (maxHScrollOffset() != maxHBefore)
                emit hScrollRangeChanged(maxHScrollOffset());
            setScrollOffset(scrollOffset_);
            followCursorHorizontally();
        }
    }
    update();
}
```

（该网格重算块在 resizeEvent/setTerminalFontPointSize/showEvent 三处重复——抽私有方法 `recomputeGridForCurrentSize()` 统一，三处调用。）

- [ ] **步骤 4：运行验证通过 + 全量回归 + Commit**

```bash
git add src/terminal/ZzCoreTerminalWidget.h src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "feat(terminal): Z2 任务 7 Ctrl+滚轮缩放与隐藏标签页字号滞留修复"
```

- [ ] **步骤 5：用户手测确认**

Ctrl+滚轮放大缩小、边界钳制；开两个标签，A 标签改设置字号后切走再切回，字号与网格一致。

---

## 任务 8：最小网格约束（24×6 等价物）

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.h`、`src/terminal/ZzCoreTerminalWidget.cpp`（resizeEvent 区、新增 minimumSizeHint）
- 测试：`tests/unit/tst_ZzCoreTerminalWidget.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
    void minimumGridClamped()
    {
        ZzCoreTerminalWidget widget;
        widget.show();
        widget.resize(5 * widget.cellWidthForTest(),
                      2 * widget.cellHeightForTest());
        QCOMPARE(widget.gridCols(), 24); // fork setMinimumTerminalSize(24,6) 等价
        QCOMPARE(widget.gridRows(), 6);
        QVERIFY(widget.minimumSizeHint().width() >= 24 * widget.cellWidthForTest());
        QVERIFY(widget.minimumSizeHint().height() >= 6 * widget.cellHeightForTest());
    }
```

- [ ] **步骤 2：运行验证失败**

预期：FAIL（当前 qMax(1,…) 口径给出 5×2）。

- [ ] **步骤 3：实现**

`ZzCoreTerminalWidget.h` public 追加：

```cpp
    /** @brief 最小网格 24×6 换算的最小尺寸提示（fork 防护语义等价物）。 */
    [[nodiscard]] QSize minimumSizeHint() const override;
```

`ZzCoreTerminalWidget.cpp`：

```cpp
QSize ZzCoreTerminalWidget::minimumSizeHint() const
{
    return {24 * cellWidth_, 6 * cellHeight_};
}
```

网格重算方法（任务 7 抽出的 `recomputeGridForCurrentSize()`）与 resizeEvent 中：

```cpp
    const int cols = qMax(24, size().width() / cellWidth_);   // 最小 24 列
    const int rows = qMax(6,  size().height() / cellHeight_); // 最小 6 行
```

（控件被布局压得更小时网格保持 24×6，多余内容裁剪显示——防护语义优先于像素贴合。）

- [ ] **步骤 4：运行验证通过 + 全量回归 + Commit**

注意：`tst_ZzCoreTerminalWidget` 既有小尺寸用例（如 resizeEmitsGrid 用 100×30，不受影响；若有 <24×6 的既有断言需同步更新为钳制口径）。

```bash
git add src/terminal/ZzCoreTerminalWidget.h src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "feat(terminal): Z2 任务 8 最小网格 24x6 约束"
```

- [ ] **步骤 5：用户手测确认**

窗口缩到极小不崩、网格不再低于 24×6；分屏到窄窗格行为可接受。

---

## 任务 9：Alternate 屏滚轮转方向键

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.cpp`（:612-626 wheelEvent）
- 测试：`tests/unit/tst_ZzCoreTerminalWidget.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
    void alternateScreenWheelSendsArrowKeys()
    {
        ZzCoreTerminalWidget widget;
        widget.recvData(QByteArray("\x1b[?1049h")); // 进 Alternate 屏
        QSignalSpy spy(&widget, &ZzCoreTerminalWidget::sendData);
        QWheelEvent up(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0),
                       QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(&widget, &up);
        QVERIFY(spy.count() >= 1);
        QByteArray all;
        for (const auto &call : spy)
            all += call.at(0).toByteArray();
        // 每滚轮档 3 行：3 次 ↑（普通模式 ESC[A，应用光标模式 ESC OA）
        QVERIFY(all.count('A') >= 3);
        widget.recvData(QByteArray("\x1b[?1049l")); // 回 Primary
    }
```

- [ ] **步骤 2：运行验证失败**

预期：FAIL（现行 Alternate 分支 `event->ignore()` 直接返回，spy 为空）。

- [ ] **步骤 3：实现**

`wheelEvent` Alternate 分支（:614-617）替换为：

```cpp
    if (term_.isAlternateScreen()) {
        // Alternate 无历史：滚轮转方向键发给会话（vim/less 翻行），每档 3 行。
        // Z3 鼠标上报落地后此处需加「上报开启时不转」分支（登记，不在本任务）。
        const int steps = event->angleDelta().y() / 120;
        if (steps != 0) {
            ZzKeyEvent keyEvent;
            keyEvent.action    = ZzKeyEvent::Action::Press;
            keyEvent.modifiers = ZzKeyModifier::None;
            keyEvent.key       = steps > 0 ? ZzKeyEvent::Key::Up
                                           : ZzKeyEvent::Key::Down;
            for (int i = 0, n = qAbs(steps) * 3; i < n; ++i)
                term_.sendKey(keyEvent);
            event->accept();
            return;
        }
        event->ignore();
        return;
    }
```

- [ ] **步骤 4：运行验证通过 + 全量回归 + Commit**

```bash
git add src/terminal/ZzCoreTerminalWidget.cpp tests/unit/tst_ZzCoreTerminalWidget.cpp
git commit -m "feat(terminal): Z2 任务 9 Alternate 屏滚轮转方向键"
```

- [ ] **步骤 5：用户手测确认**

vim/less 中滚轮逐行翻页；Primary 屏滚轮仍是历史滚动（任务 5 口径）。

---

## 自检记录

- **规格覆盖**：8 单元 → 任务 1/2（单元 1）、3（单元 2）、4（单元 3）、5（单元 4）、6（单元 5）、7（单元 6）、8（单元 7）、9（单元 8）。单元 1 拆两任务（palette 单元可独立于接线被否决/批准）。
- **勘误 E-1**（已同步规格 §4.4）：总体设计 §3.4 假设读回注入用既有 `prependPrimaryLines`，核实其语义为「屏幕顶部插入、溢出裁回历史**最新端**」（M16b 接缝归还专用），用于读回会顺序倒置。ZzScrollback 无最旧端注入公共 API → 任务 5 前置 ZzTermCore M19 原语里程碑（验收标准见任务 5 前置门槛）。
- **命名一致性**：`setColorScheme`/`colorSchemeName`（任务 2 定义，任务 2 测试使用）；`historyTopReached`/`notifyHistoryPrepended`/`injectOlderBlock`/`hasSelectionForMenu`/`selectAll`/`copySelectionToClipboard`/`recomputeGridForCurrentSize`/`prependHistoryLines`（M19，以 Core 落地签名为准）全计划一致。
- **风险登记**：任务 3 若 CI 缺 Core5Compat 组件属环境问题，报用户裁决；任务 5 桥测试的非对称构造（引擎预置冷行）由执行者按既有测试脚手架落实。
