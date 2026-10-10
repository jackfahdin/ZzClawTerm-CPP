#include <QtTest/QtTest>

#include <QClipboard>

#include <ZzTerm/UnwrapView.h> // lineCount() 需完整类型（Terminal.h 仅前向声明）

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
};

QTEST_MAIN(tst_ZzCoreTerminalWidget)
#include "tst_ZzCoreTerminalWidget.moc"
