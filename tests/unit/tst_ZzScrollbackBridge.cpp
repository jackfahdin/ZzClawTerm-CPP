#include <QtTest/QtTest>

#include <QtCore/QStandardPaths>

#include "log/ZzLogEngine.h"
#include "terminal/ZzCoreTerminalWidget.h"
#include "terminal/ZzScrollbackBridge.h"

/**
 * @brief 滚动历史桥新口径（Z1 落盘换源后）：scrollout 定稿行经 feed 差分
 *        入引擎、读回透传、降级信号透传、跨重启行号重对齐（规格 §5.4/§八）。
 *
 * 追加路径由 ZzCoreTerminalWidget::contentFed 驱动：feed 后对 HistoryView
 * 做绝对行号差分（droppedLineCount 对账），新滚出的小屏幕定稿行取纯文本
 * appendLine。测试只覆盖无 reflow 的行变 scrollout 场景——列变 reflow
 * 会改变 HistoryView lineCount 而 droppedLineCount 不计重组，差分对账在
 * reflow 后的重复/缺口属已知设计级遗留（Z2 收口），此处不钉死。
 *
 * 读回口径：返回绝对行号 [beforeLine - maxLines, beforeLine) 窗口内的行
 * 文本，旧→新顺序（经引擎 getLines 夹取到当前可读窗口）。
 */
class tst_ZzScrollbackBridge : public QObject
{
    Q_OBJECT
private slots:
    void feedScrolloutAppendsToEngine()
    {
        ZzCoreTerminalWidget term;
        term.terminal().resize(10, 3); // 小屏：3 行起滚
        ZzLogEngine engine(ZzLogEngine::Config{}); // 温层路径为空 → 纯内存模式
        QVERIFY(engine.open());
        ZzScrollbackBridge bridge(&term, &engine);

        // 3 行屏喂 6 行（行尾 \r\n）：l1..l4 滚入历史
        term.recvData(QByteArray("l1\r\nl2\r\nl3\r\nl4\r\nl5\r\nl6\r\n"));
        QCOMPARE(engine.totalLines(), 4ULL);
        QCOMPARE(bridge.readOlderLines(4, 10),
                 QStringList({QStringLiteral("l1"), QStringLiteral("l2"),
                              QStringLiteral("l3"), QStringLiteral("l4")}));

        // 第二次 feed：差分游标续走，l5/l6 追加，不重复不缺口
        term.recvData(QByteArray("l7\r\nl8\r\n"));
        QCOMPARE(engine.totalLines(), 6ULL);
        QCOMPARE(bridge.readOlderLines(6, 10),
                 QStringList({QStringLiteral("l1"), QStringLiteral("l2"),
                              QStringLiteral("l3"), QStringLiteral("l4"),
                              QStringLiteral("l5"), QStringLiteral("l6")}));
    }

    void readBackDelegates()
    {
        ZzCoreTerminalWidget term;
        ZzLogEngine engine(ZzLogEngine::Config{});
        QVERIFY(engine.open());
        engine.appendLine({QStringLiteral("line-1"), QByteArray()});
        engine.appendLine({QStringLiteral("line-2"), QByteArray()});
        engine.appendLine({QStringLiteral("line-3"), QByteArray()});
        ZzScrollbackBridge bridge(&term, &engine);
        // [beforeLine - maxLines, beforeLine) = 绝对行号 {1, 2}
        QCOMPARE(bridge.readOlderLines(3, 2),
                 QStringList({QStringLiteral("line-2"),
                              QStringLiteral("line-3")}));
    }

    void degradationForwards()
    {
        ZzCoreTerminalWidget term;
        ZzLogEngine engine(ZzLogEngine::Config{});
        QVERIFY(engine.open());
        ZzScrollbackBridge bridge(&term, &engine);
        QSignalSpy spy(&bridge, &ZzScrollbackBridge::degraded);
        bridge.simulateDegradationForTest(QStringLiteral("磁盘空间不足"));
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.first().at(0).toString().contains(QStringLiteral("磁盘")));
    }

    /**
     * @brief 回归：跨重启重开同一会话时行号从 0 重新对齐，不恢复旧行。
     *        旧口径（dupDisplayOutput 驱动）的等价场景，改由 feed 驱动：
     *        会话 1 小热层强制归档，析构 flush 落冷层；会话 2 同 sessionId
     *        重开，冷层旧行不在本会话映射内不可见，行号重新从 0 起。
     */
    void scrollbackRestartsWithFreshLineNumbers()
    {
        QStandardPaths::setTestModeEnabled(true); // 隔离用户真实配置目录
        const QString sessionId = QStringLiteral("test-bridge-restart-align");
        const QString dirPath =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/scrollback");
        QDir().mkpath(dirPath);
        // 清掉历史运行残留的温层文件，避免崩溃恢复续传干扰计数断言
        for (const QString &file :
             QDir(dirPath).entryList({sessionId + QStringLiteral("-*.warm")},
                                     QDir::Files)) {
            QFile::remove(dirPath + QLatin1Char('/') + file);
        }

        const auto makeConfig = [&](const QString &warmName) {
            ZzLogEngine::Config config;
            config.hotCapacity = 64;   // 小热层：几十行即强制归档进温层
            config.archiveBatchSize = 16;
            config.warmFilePath = dirPath + QLatin1Char('/') + warmName;
            config.coldDbPath = dirPath + QStringLiteral("/cold.db");
            config.sessionId = sessionId;
            return config;
        };

        // 会话 1：feed 200 行制造 scrollout，引擎归档；析构 flush 落冷层
        {
            ZzCoreTerminalWidget term;
            term.terminal().resize(10, 3);
            ZzLogEngine engine(makeConfig(sessionId + QStringLiteral("-s1.warm")));
            QVERIFY(engine.open());
            ZzScrollbackBridge bridge(&term, &engine);
            for (int i = 0; i < 200; ++i) {
                term.recvData(
                    QStringLiteral("old-line-%1\r\n").arg(i).toUtf8());
            }
            QVERIFY(bridge.totalLines() > 0);
        }

        // 会话 2（等价重启后重开）：行号从 0 起，不恢复上一会话行
        ZzCoreTerminalWidget term2;
        term2.terminal().resize(10, 3);
        ZzLogEngine engine2(makeConfig(sessionId + QStringLiteral("-s2.warm")));
        QVERIFY(engine2.open());
        ZzScrollbackBridge bridge2(&term2, &engine2);
        QCOMPARE(bridge2.totalLines(), qint64(0));

        // 3 行屏：第 3 个 \r\n 挤出首行 scrollout（末尾 "c" 不换行留在屏上）
        term2.recvData(QByteArray("fresh-line\r\na\r\nb\r\nc"));
        QCOMPARE(bridge2.totalLines(), qint64(1));
        QCOMPARE(bridge2.readOlderLines(1, 1),
                 QStringList({QStringLiteral("fresh-line")}));
    }
};

QTEST_MAIN(tst_ZzScrollbackBridge)
#include "tst_ZzScrollbackBridge.moc"
