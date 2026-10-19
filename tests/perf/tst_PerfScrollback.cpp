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
