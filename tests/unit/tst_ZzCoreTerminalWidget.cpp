#include <QtTest/QtTest>

#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QWheelEvent>

#include <ZzTerm/UnwrapView.h> // lineCount() 需完整类型（Terminal.h 仅前向声明）

#include "terminal/ZzCoreTerminalWidget.h"
#include "terminal/ZzTerminalPalette.h"

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
        widget.show(); // 隐藏 widget 的 resize() 不投递 QResizeEvent（Qt 延迟到 show）
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

    void fontSizeChangeRecomputesGrid()
    {
        ZzCoreTerminalWidget widget;
        widget.show(); // 隐藏 widget 的 resize() 不投递 QResizeEvent（Qt 延迟到 show）
        const int cw = widget.cellWidthForTest();
        const int ch = widget.cellHeightForTest();
        widget.resize(100 * cw, 30 * ch);
        QCOMPARE(widget.gridCols(), 100);

        // 字号变大 → cell 像素变大 → 同控件尺寸下列数必须重算变小，
        // 且走 gridResized 信号链路（宿主据此转发 transport->resize）。
        QSignalSpy spy(&widget, &ZzCoreTerminalWidget::gridResized);
        const int ps = widget.font().pointSize();
        widget.setTerminalFontPointSize(ps > 0 ? ps * 4 : 48);
        QVERIFY(widget.gridCols() < 100);
        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toInt(), widget.gridCols());
    }

    void unwrapModeStitchesChains()
    {
        ZzCoreTerminalWidget widget;
        widget.terminal().resize(10, 4);
        // 对齐 spike 已验证口径：末尾 "b\r\n" 使 4 行屏滚出一行入历史，
        // 拼接空间共 [x 链, b, 空] 3 条（喂到 \r\n 为止只有 2 条）。
        widget.recvData(QByteArray("xxxxxxxxxxxxxxxxxxxxxxxxx\r\nb\r\n")); // 25 x
        widget.setUnwrapMode(true);
        QCOMPARE(widget.terminal().unwrapView().lineCount(), std::size_t(3));
        QCOMPARE(widget.maxHScrollOffset(), 25 - 10);
    }

    void colorSchemeAppliesToRendering()
    {
        ZzCoreTerminalWidget widget;
        widget.setColorScheme(QStringLiteral("Builtin Tango Dark"));
        // 方案切换后默认背景色变化即生效（paintEvent 全铺背景）
        widget.setColorScheme(QStringLiteral("iTerm2 Solarized Light"));
        // 不断言像素（offscreen 无意义），断言内部态与无崩溃：
        QCOMPARE(widget.colorSchemeName(), QStringLiteral("iTerm2 Solarized Light"));
    }

    void unknownSchemeFallsBack()
    {
        ZzCoreTerminalWidget widget;
        widget.setColorScheme(QStringLiteral("NoSuchScheme-xyz"));
        QCOMPARE(widget.colorSchemeName(),
                 ZzTerminalPalette::defaultSchemeName());
    }

    void imeCommitSendsText()
    {
        ZzCoreTerminalWidget widget;
        QSignalSpy spy(&widget, &ZzCoreTerminalWidget::sendData);
        QInputMethodEvent commit(QString(), QList<QInputMethodEvent::Attribute>{});
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

    void wheelAtHistoryTopEmitsTopReached()
    {
        ZzCoreTerminalWidget widget;
        widget.show(); // 隐藏 widget 的 resize() 不投递 QResizeEvent（Qt 延迟到 show）
        for (int i = 0; i < 60; ++i)
            widget.recvData(QByteArray("line\r\n"));
        QVERIFY(widget.maxScrollOffset() > 0); // 60 行必然滚出历史（屏 ≤ 60 行）
        QSignalSpy spy(&widget, &ZzCoreTerminalWidget::historyTopReached);
        widget.setScrollOffset(widget.maxScrollOffset()); // 到顶
        QWheelEvent up(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0),
                       QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(&widget, &up);
        QVERIFY(spy.count() >= 1);
        // 未到顶的上滚不触发（信号语义 = 越顶读回触发点，非每次上滚）
        QSignalSpy spy2(&widget, &ZzCoreTerminalWidget::historyTopReached);
        widget.setScrollOffset(0);
        QWheelEvent up2(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0),
                        QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                        Qt::NoScrollPhase, false);
        QApplication::sendEvent(&widget, &up2);
        QCOMPARE(spy2.count(), 0);
    }
};

QTEST_MAIN(tst_ZzCoreTerminalWidget)
#include "tst_ZzCoreTerminalWidget.moc"
