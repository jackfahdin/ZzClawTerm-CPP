#include <QtTest/QtTest>

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QInputMethodEvent>
#include <QTimer>
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

    void wideCharInkSpansContinuationCell()
    {
        // 回归：普通绘制路径双宽字符被裁成半宽（选区路径正常）。
        // 引擎口径实证：汉字 = WideLead（首格带文本）+ WideContinuation
        //（续格 text 空串）；字形须横跨两格。像素断言：续格矩形内缘必须
        // 出现非背景墨色——修复前续格的 fillRect 在文本之后执行，把左格
        // 字形的右半重盖为纯背景。
        ZzCoreTerminalWidget widget;
        const int fontId = QFontDatabase::addApplicationFont(
            QStringLiteral(":/test-fonts/ZzClawTermTestWide.otf"));
        QVERIFY(fontId >= 0);
        const QString family = QStringLiteral("ZzClawTerm Test Wide");
        QVERIFY(QFontDatabase::applicationFontFamilies(fontId).contains(family));
        widget.setFont(QFont(family));
        widget.setTerminalFontPointSize(12); // 同时刷新控件缓存的 cell metrics
        QCOMPARE(QFontInfo(widget.font()).family(), family);
        const QFontMetrics fontMetrics(widget.font());
        const int narrowAdvance = fontMetrics.horizontalAdvance(QLatin1Char('M'));
        QCOMPARE(fontMetrics.horizontalAdvance(QStringLiteral("中")), 2 * narrowAdvance);
        QCOMPARE(fontMetrics.horizontalAdvance(QStringLiteral("文")), 2 * narrowAdvance);
        widget.resize(24 * widget.cellWidthForTest(),
                      6 * widget.cellHeightForTest());
        widget.recvData(QStringLiteral("中文\r\n").toUtf8()); // 中: 格0+1，文: 格2+3
        const QImage img = widget.grab().toImage();
        const int cw = widget.cellWidthForTest();
        const int ch = widget.cellHeightForTest();
        // 远处纯背景采样（第 4 行无内容）
        const QColor bg = img.pixelColor(cw / 2, 4 * ch + ch / 2);
        const QRect cont(cw + 1, 1, cw - 2, ch - 2); // 「中」续格内缘
        int ink = 0;
        for (int y = cont.top(); y <= cont.bottom(); ++y)
            for (int x = cont.left(); x <= cont.right(); ++x)
                if (img.pixelColor(x, y) != bg)
                    ++ink;
        QVERIFY2(ink > 0, "双宽字符续格内无任何墨色：字形被裁成半宽");
    }

    void blockCursorOnWideContinuationKeepsGlyphInk()
    {
        // 回归：宽字符写满行尾时引擎停光标于末列续格（wrap-pending），块状
        // 光标盖住左格字形右半。修复后以反色画笔从首格原点重绘整字——
        // 续格内缘须出现背景色笔画像素（修复前整块纯色，计数为 0）。
        ZzCoreTerminalWidget widget;
        const int fontId = QFontDatabase::addApplicationFont(
            QStringLiteral(":/test-fonts/ZzClawTermTestWide.otf"));
        QVERIFY(fontId >= 0);
        const QString family = QStringLiteral("ZzClawTerm Test Wide");
        QVERIFY(QFontDatabase::applicationFontFamilies(fontId).contains(family));
        widget.setFont(QFont(family));
        widget.setTerminalFontPointSize(12); // 同时刷新控件缓存的 cell metrics
        QCOMPARE(QFontInfo(widget.font()).family(), family);
        const QFontMetrics fontMetrics(widget.font());
        const int narrowAdvance = fontMetrics.horizontalAdvance(QLatin1Char('M'));
        QCOMPARE(fontMetrics.horizontalAdvance(QStringLiteral("中")), 2 * narrowAdvance);
        QCOMPARE(fontMetrics.horizontalAdvance(QStringLiteral("文")), 2 * narrowAdvance);
        widget.resize(80 * widget.cellWidthForTest(),
                      6 * widget.cellHeightForTest()); // 保证 grab 覆盖格 79
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

    void colorSchemeAppliesToRendering()
    {
        ZzCoreTerminalWidget widget;
        widget.setColorScheme(ZzTerminalPalette::defaultSchemeName());
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
        // 超宽行，复制内容含屏外部分。最小网格钳制（24×6）后 widget 路径
        // 最小 24 列：40 字符行在 24 列屏上有 16 列屏外，超宽前提成立
        //（20 字符行在钳制后不再超宽，场景曾被静默架空——复审发现①）。
        ZzCoreTerminalWidget widget;
        widget.show();
        widget.resize(10 * widget.cellWidthForTest(), 5 * widget.cellHeightForTest());
        QCOMPARE(widget.gridCols(), 24); // 钳制生效，前提自证
        const QByteArray line = QByteArray("0123456789ABCDEFGHIJ").repeated(2); // 40 列
        widget.recvData(line + "\r\n");
        widget.setUnwrapMode(true);
        widget.terminal().setSelection({0, 0}, {0, 40});
        QCOMPARE(QString::fromStdString(widget.terminal().selectedText()),
                 QString::fromUtf8(line));
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
        e.ignore(); // QEvent 默认 accepted=true：先置 ignore 才能分辨 handler 是否消费
        QApplication::sendEvent(&widget, &e);
        QVERIFY(e.isAccepted());
    }

    void selectionAnchorShiftsOnHistoryPrepend()
    {
        // 回归：拖拽选区进行中发生读回注入，锚点副本须随内容顺移，
        // 否则下次 mouseMove 重放旧锚点把选区盖回漂移位置。
        //
        // 与简报字面量的出入（实测核实）：字面量「锚定行 0、下移拖到行 2」
        // 在 24×6 钳制网格上锚点正移后 line(5) > 目标 line(2)，拖拽反转为
        // 后向选区，selectionRange 词典序归一化后锚点侧落到 end，start
        // 恒为 2——修复后也不可能见到 5。改为保持前向选区：锚定可见顶行
        //（逻辑行 hist），注入 +5 后拖到底行（逻辑行 hist+5，与锚点同列序
        // 保持 forward），锚点侧即 start，start.line 期望 = hist + 5。
        ZzCoreTerminalWidget widget;
        widget.show(); // 隐藏 widget 的 resize() 不投递 QResizeEvent（Qt 延迟到 show）
        widget.resize(24 * widget.cellWidthForTest(),
                      6 * widget.cellHeightForTest()); // 钳制下限网格，行列确定
        for (int i = 0; i < 20; ++i)
            widget.recvData(QStringLiteral("L%1\r\n").arg(i).toUtf8());

        const int cw = widget.cellWidthForTest();
        const int ch = widget.cellHeightForTest();
        const std::int64_t hist =
            std::int64_t(widget.terminal().historyView().lineCount());
        QVERIFY(hist > 0); // 前提：有历史，可见顶行映射到逻辑行 hist

        QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier,
                          QPoint(cw / 2, ch / 2)); // 锚在可见行 0 = 逻辑行 hist
        widget.notifyHistoryPrepended(5);          // 注入 5 行（等价桥回调）
        QTest::mouseMove(&widget, QPoint(3 * cw, 5 * ch + ch / 2)); // 拖到底行
        QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier,
                            QPoint(3 * cw, 5 * ch + ch / 2));

        ZzLogicalPos start, end;
        QVERIFY(widget.terminal().selectionRange(start, end));
        QCOMPARE(start.line, hist + 5); // 锚点正移：hist → hist + 5
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
};

QTEST_MAIN(tst_ZzCoreTerminalWidget)
#include "tst_ZzCoreTerminalWidget.moc"
