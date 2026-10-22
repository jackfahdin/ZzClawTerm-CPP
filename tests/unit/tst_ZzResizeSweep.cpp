// 全链路 resize 扫掠复现（2026-10-22 用户实测症状）：
// ZzTerminalView + ZzLocalPtyTransport（真实 bash PTY + 150ms transport 去抖）
// + QWidget 像素级连续 resize——与生产拖拽路径逐点一致。
// 满屏 ls 后反复缩扩，稳定后提示符必须贴底（cursor row == rows-1、底部无空行）。
//
// 环境健壮性（CI ubuntu-24.04 红后加固）：
// - PS1 显式钉死（不依赖 runner bashrc/hostname/color 探测），PROMPT_COMMAND 卸除；
// - 所有终态断言走 QTRY 收敛（等系统停到期望态，容忍慢 runner 上的迟到重绘），
//   不做 quiet 窗口后的单点快照断言。
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

    // 仅光标贴底的收敛判据（用于提示符被 readline 隐藏的窄窗态——底行合法为空）。
    static bool cursorAtBottom(ZzCoreTerminalWidget *w)
    {
        return w->terminal().renderView().cursor().position.row
               == w->terminal().size().rows - 1;
    }

    // 收敛判据：光标贴底且底行非空（慢 runner 上 QTRY 轮询到该态为止）。
    static bool settledAtBottom(ZzCoreTerminalWidget *w)
    {
        if (!cursorAtBottom(w))
            return false;
        return !w->visibleLineTextForTest(w->terminal().size().rows - 1)
                    .trimmed()
                    .isEmpty();
    }

    static void dumpBottom(ZzCoreTerminalWidget *w, const char *tag)
    {
        const int rowsNow = w->terminal().size().rows;
        qInfo("%s: cursor row=%d rows=%d", tag,
              w->terminal().renderView().cursor().position.row, rowsNow);
        for (int r = qMax(0, rowsNow - 5); r < rowsNow; ++r)
            qInfo("  S%d='%s'", r,
                  w->visibleLineTextForTest(r).toUtf8().constData());
    }

    void runSweep(const char *backend)
    {
        qputenv("ZZ_TERM_BACKEND", backend);

        // 深目录：满屏 ls（120 个文件溢出 50 行屏进历史）
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
        QTRY_VERIFY_WITH_TIMEOUT(transport->state() == ZzTransportInterface::State::Connected,
                                 15000);

        view.show();
        auto *w = view.termWidget();
        const int cw = w->cellWidthForTest();
        const int ch = w->cellHeightForTest();
        view.resize(220 * cw, 55 * ch); // 视图含滚动条/边距：取实际网格为满屏口径
        int maxCols = 0, maxRows = 0;
        QTRY_VERIFY_WITH_TIMEOUT((maxCols = w->terminal().size().cols) > 150, 15000);
        QTRY_VERIFY_WITH_TIMEOUT((maxRows = w->terminal().size().rows) > 40, 15000);
        const QSize fullViewSize = view.size();

        // 钉死 PS1（94 可见字符 + 颜色转义，用户真实 PS1 同构；窄于 ~94 列
        // readline 水平滚动隐藏提示符），卸除 PROMPT_COMMAND 防每提示符覆写。
        // 提示符长度与 runner 的 bashrc/hostname/颜色探测全解耦。
        transport->write(
            "unset PROMPT_COMMAND\n"
            "PS1='\\[\\e[00;35m\\]Jackfahdin\\[\\e[00m\\]\\[\\e[00;33m\\]@\\[\\e[00m\\]"
            "\\[\\e[00;32m\\]zz\\[\\e[00m\\]: \\[\\e[01;34m\\]"
            "/tmp/Jackfahdin/github/ZzClawTerm/long/path/to/the/filler\\[\\e[00m\\] $ '\n");
        // 满屏 ls：cd 深目录后 ls 溢出进历史
        transport->write(QStringLiteral("cd %1 && ls -la\n").arg(deep).toUtf8());
        QTRY_VERIFY_WITH_TIMEOUT(settledAtBottom(w), 30000);
        dumpBottom(w, "after ls");

        const auto cursorRow = [w] {
            return w->terminal().renderView().cursor().position.row;
        };
        const auto bottomText = [w] {
            return w->visibleLineTextForTest(w->terminal().size().rows - 1);
        };

        // 用户拖拽模拟：像素级随机游走缩扩（约 30 列 ~ 满屏），
        // 间隔 20-120ms 不等；间或停顿 300ms 让 transport 去抖送达中间尺寸。
        std::mt19937 rng(20261022);
        int cols = maxCols, rows = maxRows;
        const int minCols = 30, minRows = 12; // 拖窄到提示符必然折行/隐藏的宽度
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
        QTRY_VERIFY_WITH_TIMEOUT(settledAtBottom(w), 30000);
        dumpBottom(w, "after sweep");

        // 相位 A：打到 24x6 前端最小钳制（长提示符折成多行占满小窗）再拉回
        for (int round = 0; round < 3; ++round) {
            view.resize(24 * cw, 6 * ch);
            QTest::qWait(400); // 去抖 settled → bash 在小窗重绘
            view.resize(fullViewSize);
            QTRY_VERIFY_WITH_TIMEOUT(settledAtBottom(w), 30000);
            char tag[32];
            std::snprintf(tag, sizeof(tag), "phaseA-%d", round);
            dumpBottom(w, tag);
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
            QTRY_VERIFY_WITH_TIMEOUT(settledAtBottom(w), 30000);
            char tag[32];
            std::snprintf(tag, sizeof(tag), "phaseB-%d", round);
            dumpBottom(w, tag);
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
        QTRY_VERIFY_WITH_TIMEOUT(settledAtBottom(w), 30000);
        dumpBottom(w, "phaseC");

        // 相位 D（用户慢拖高保真 + 探针剧本）：下行慢扫掠（步停 250ms，去抖
        // 每步 settled → bash 每步重绘）到窄窗，稳定后上行慢扫掠拉回。
        // 长提示符窄于可见宽度时 readline 水平滚动隐藏提示符（重绘退化为
        // `\r<字面空格>\r` 清行），拉宽越过阈值后在旧锚定行重印——
        // pre-fix 引擎此后每次增行都卡死（用户实测症状），修复后应贴底。
        {
            for (int c = maxCols - 4; c >= 44; c -= 4) {
                const int r = maxRows - (maxCols - c) * (maxRows - 28)
                                        / (maxCols - 44);
                view.resize(c * cw, r * ch);
                QTest::qWait(250);
            }
            waitQuiet(w, 500, 15000); // 窄窗稳定：此处不做贴底断言——残行锚定
            // 态下光标合法停在末行之上（修复保证的是拉回满屏后的沉底）
            dumpBottom(w, "phaseD settle small");
            const int curRows = w->terminal().size().rows;
            const int curCols = w->terminal().size().cols;
            for (int c = curCols + 4; c <= maxCols; c += 4) {
                const int r = curRows + (c - curCols) * (maxRows - curRows)
                                      / (maxCols - curCols);
                view.resize(c * cw, r * ch);
                QTest::qWait(250);
            }
            view.resize(fullViewSize);
            QTRY_VERIFY_WITH_TIMEOUT(settledAtBottom(w), 30000);
            dumpBottom(w, "phaseD");
        }

        transport->write("exit\n");
        waitQuiet(w, 200, 3000);
    }
};

QTEST_MAIN(tst_ZzResizeSweep)
#include "tst_ZzResizeSweep.moc"
