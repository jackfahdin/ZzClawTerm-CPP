// 全链路 resize 扫掠复现（2026-10-22 用户实测症状）：
// ZzTerminalView + ZzLocalPtyTransport（真实 bash PTY + 150ms transport 去抖）
// + QWidget 像素级连续 resize——与生产拖拽路径逐点一致。
// 满屏 ls 后反复缩扩，稳定后提示符必须贴底（cursor row == rows-1、底部无空行）。
#include <QtTest/QtTest>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include "terminal/ZzCoreTerminalWidget.h"
#include "terminal/ZzTerminalView.h"
#include "transport/ZzLocalPtyTransport.h"
#include "transport/ZzTransportEndpoint.h"

class tst_ZzResizeSweep : public QObject
{
    Q_OBJECT
private slots:
    void sweepContour() { runSweep("contour"); }
    void sweepNative() { runSweep("native"); }

private:
    // 等输出安静：quietMs 内无 contentFed 即返回（上限 maxMs）。
    static void waitQuiet(ZzCoreTerminalWidget *w, int quietMs, int maxMs)
    {
        bool fed = false;
        QObject::connect(w, &ZzCoreTerminalWidget::contentFed, w,
                         [&fed] { fed = true; });
        int waited = 0;
        while (waited < maxMs) {
            fed = false;
            QTest::qWait(quietMs);
            waited += quietMs;
            if (!fed)
                return;
        }
    }

    void runSweep(const char *backend)
    {
        qputenv("ZZ_TERM_BACKEND", backend);

        // 深目录：提示符 50+ 字符（对齐用户实测的长路径提示符），窄窗必折行
        QTemporaryDir deepBase;
        QVERIFY(deepBase.isValid());
        const QString deep = deepBase.filePath(
            QStringLiteral("Jackfahdin/github/ZzClawTerm/long/path/filler"));
        QVERIFY(QDir().mkpath(deep));
        for (int i = 0; i < 120; ++i) {
            QFile f(QDir(deep).filePath(QStringLiteral("file-%1.txt").arg(i)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }

        ZzTerminalView view;
        auto *transport = new ZzLocalPtyTransport(&view);
        ZzTransportEndpoint endpoint;
        endpoint.localShell = true;
        endpoint.shellProgram = QStringLiteral("/bin/bash");
        endpoint.cols = 200;
        endpoint.rows = 50;
        view.setTransport(transport);
        transport->open(endpoint);
        QTRY_VERIFY(transport->state() == ZzTransportInterface::State::Connected);

        view.show();
        auto *w = view.termWidget();
        const int cw = w->cellWidthForTest();
        const int ch = w->cellHeightForTest();
        view.resize(220 * cw, 55 * ch); // 视图含滚动条/边距：取实际网格为满屏口径
        int maxCols = 0, maxRows = 0;
        QTRY_VERIFY((maxCols = w->terminal().size().cols) > 150);
        QTRY_VERIFY((maxRows = w->terminal().size().rows) > 40);
        const QSize fullViewSize = view.size();
        waitQuiet(w, 300, 8000); // 启动横幅+首个提示符

        // 满屏 ls：先 cd 深目录（长提示符），再 ls 溢出进历史
        transport->write(QStringLiteral("cd %1 && ls -la\n").arg(deep).toUtf8());
        waitQuiet(w, 400, 15000);

        const auto bottomText = [w] {
            return w->visibleLineTextForTest(w->terminal().size().rows - 1);
        };
        const auto cursorRow = [w] {
            return w->terminal().renderView().cursor().position.row;
        };
        qInfo("after ls: cursor row=%d bottom='%s'", cursorRow(),
              bottomText().toUtf8().constData());

        // 用户拖拽模拟：像素级随机游走缩扩（约 1/3 屏 ~ 满屏），
        // 间隔 20-120ms 不等；间或停顿 300ms 让 transport 去抖送达中间尺寸。
        std::mt19937 rng(20261022);
        int cols = maxCols, rows = maxRows;
        const int minCols = 30, minRows = 12; // 拖窄到提示符必然折行的宽度
        for (int step = 0; step < 50; ++step) {
            const int dCols = static_cast<int>(rng() % 25) - 12; // ±12
            const int dRows = static_cast<int>(rng() % 11) - 5;  // ±5
            cols = std::clamp(cols + dCols, minCols, maxCols);
            rows = std::clamp(rows + dRows, minRows, maxRows);
            view.resize(cols * cw, rows * ch);
            const int pause = 20 + static_cast<int>(rng() % 100);
            QTest::qWait(pause);
            if (rng() % 6 == 0)
                QTest::qWait(300); // 停顿：去抖 settled → bash 中途重绘
        }
        view.resize(fullViewSize);
        waitQuiet(w, 500, 15000);

        const int termRows = w->terminal().size().rows;
        const auto dumpBottom = [&](const char *tag) {
            const int rowsNow = w->terminal().size().rows;
            qInfo("%s: cursor row=%d rows=%d", tag, cursorRow(), rowsNow);
            for (int r = rowsNow - 5; r < rowsNow; ++r)
                qInfo("  S%d='%s'", r,
                      w->visibleLineTextForTest(r).toUtf8().constData());
        };
        dumpBottom("after sweep");
        QCOMPARE(cursorRow(), termRows - 1);                 // 光标贴底
        QVERIFY(!bottomText().trimmed().isEmpty());          // 底部无空行

        // 相位 A：打到 24x6 前端最小钳制（长提示符折成多行占满小窗）再拉回
        for (int round = 0; round < 3; ++round) {
            view.resize(24 * cw, 6 * ch);
            QTest::qWait(400); // 去抖 settled → bash 在小窗重绘
            view.resize(fullViewSize);
            waitQuiet(w, 500, 15000);
            char tag[32];
            std::snprintf(tag, sizeof(tag), "phaseA-%d", round);
            dumpBottom(tag);
            QCOMPARE(cursorRow(), w->terminal().size().rows - 1);
            QVERIFY(!bottomText().trimmed().isEmpty());
        }

        // 相位 B：缩扩之间重跑 ls（新输出与新 resize 交错）
        for (int round = 0; round < 2; ++round) {
            transport->write("ls -la\n");
            QTest::qWait(60); // ls 打印中途开始拖
            for (int step = 0; step < 15; ++step) {
                const int c = minCols + static_cast<int>(rng() % (maxCols - minCols));
                const int r = minRows + static_cast<int>(rng() % (maxRows - minRows));
                view.resize(c * cw, r * ch);
                QTest::qWait(40);
            }
            view.resize(fullViewSize);
            waitQuiet(w, 500, 15000);
            char tag[32];
            std::snprintf(tag, sizeof(tag), "phaseB-%d", round);
            dumpBottom(tag);
            QCOMPARE(cursorRow(), w->terminal().size().rows - 1);
            QVERIFY(!bottomText().trimmed().isEmpty());
        }

        // 相位 C：上滚 8 行拖着缩扩，再回到底
        w->setScrollOffset(8);
        for (int step = 0; step < 15; ++step) {
            const int c = minCols + static_cast<int>(rng() % (maxCols - minCols));
            const int r = minRows + static_cast<int>(rng() % (maxRows - minRows));
            view.resize(c * cw, r * ch);
            QTest::qWait(40);
        }
        view.resize(fullViewSize);
        w->setScrollOffset(0);
        waitQuiet(w, 500, 15000);
        dumpBottom("phaseC");
        QCOMPARE(cursorRow(), w->terminal().size().rows - 1);
        QVERIFY(!bottomText().trimmed().isEmpty());

        // 相位 D（用户慢拖高保真 + 探针剧本）：下行慢扫掠（步停 250ms，去抖
        // 每步 settled → bash 每步重绘）到窄窗，稳定后上行慢扫掠拉回。
        // 长提示符在中等宽度折行/回收时 readline 重绘会把光标抬离末行——
        // pre-fix 引擎此后每次增行都滞留一行历史（用户实测症状），修复后应贴底。
        {
            for (int c = maxCols - 4; c >= 44; c -= 4) {
                const int r = maxRows - (maxCols - c) * (maxRows - 28)
                                        / (maxCols - 44);
                view.resize(c * cw, r * ch);
                QTest::qWait(250);
            }
            waitQuiet(w, 500, 15000);
            const int curRows = w->terminal().size().rows;
            const int curCols = w->terminal().size().cols;
            qInfo("phaseD settle small: cols=%d rows=%d cursor=%d", curCols, curRows,
                  cursorRow());
            for (int r = curRows - 6; r < curRows; ++r)
                qInfo("  settle S%d='%s'", r,
                      w->visibleLineTextForTest(r).left(60).toUtf8().constData());
            for (int c = curCols + 4; c <= maxCols; c += 4) {
                const int r = curRows + (c - curCols) * (maxRows - curRows)
                                      / (maxCols - curCols);
                view.resize(c * cw, r * ch);
                QTest::qWait(250);
                qInfo("phaseD step c=%d r=%d grid=%dx%d cursor=%d", c, r,
                      w->terminal().size().cols, w->terminal().size().rows,
                      cursorRow());
                if (c <= curCols + 16) {
                    const int rowsNow = w->terminal().size().rows;
                    for (int rr = rowsNow - 6; rr < rowsNow; ++rr)
                        qInfo("    S%d='%s'", rr,
                              w->visibleLineTextForTest(rr).left(60).toUtf8().constData());
                }
            }
            view.resize(fullViewSize);
            waitQuiet(w, 500, 15000);
            dumpBottom("phaseD");
            QCOMPARE(cursorRow(), w->terminal().size().rows - 1);
            QVERIFY(!bottomText().trimmed().isEmpty());
        }

        transport->write("exit\n");
        waitQuiet(w, 200, 3000);
    }
};

QTEST_MAIN(tst_ZzResizeSweep)
#include "tst_ZzResizeSweep.moc"
