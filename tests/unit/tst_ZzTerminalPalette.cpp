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
        auto sorted = all;
        sorted.sort(Qt::CaseInsensitive);
        QVERIFY(all == sorted);
    }

    void parsesSchemeFields()
    {
        const ZzTerminalPalette::Scheme s =
            ZzTerminalPalette::scheme(QStringLiteral("Builtin Tango Dark"));
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
