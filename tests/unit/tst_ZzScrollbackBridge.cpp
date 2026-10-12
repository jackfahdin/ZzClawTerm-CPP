#include <QtTest/QtTest>

#include <QtCore/QStandardPaths>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>

#include <ZzTerm/HistoryView.h> // ZzLineView 完整类型（Terminal.h 仅前向声明）

#include "log/ZzLogEngine.h"
#include "terminal/ZzCoreTerminalWidget.h"
#include "terminal/ZzScrollbackBridge.h"

/**
 * @brief 滚动历史桥新口径（Z1 落盘换源后）：scrollout 定稿行经 feed 差分
 *        入引擎、读回透传、降级信号透传、跨重启行号重对齐（规格 §5.4/§八）。
 *
 * 追加路径由 ZzCoreTerminalWidget::contentFed 驱动：feed 后对 HistoryView
 * 做绝对行号差分（droppedLineCount 对账），新滚出的小屏幕定稿行取纯文本
 * appendLine。变宽 reflow 使行数收缩（newest < 游标）的场景由
 * reflowResetsAppendCursor 钉死（游标回锚，Z2 任务 5 收口）；变宽以外的
 * 重组形态（变窄行数增长）超出减法对账表达能力，仍不钉死。
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

    /**
     * @brief 全链路（Z2 任务 5）：滚轮越顶 → historyTopReached → 桥从引擎
     *        读回一块注入 Core 历史最旧端 → 视口数值稳定 → 继续上滚进入
     *        注入区 → 再次越顶注入下一块（M19 减法口径：基线随注入回退）。
     *
     * 非对称构造：110 块 × 100 行喂入 10x3 小屏，Core 构造期容量 10000
     * 被充满并裁掉前 997 行（已落盘引擎），注入归还额度由此而来。
     * 分块喂入而非单次巨喂：contentFed 差分游标一次最多同步当前可见
     * 历史窗口，单次喂超容量会触发「缺口跳齐」吞掉落盘段。
     */
    void scrollTopInjectsOlderLines()
    {
        ZzCoreTerminalWidget term;
        term.terminal().resize(10, 3);
        ZzLogEngine::Config config;
        config.hotCapacity = 20000; // 纯内存模式热层须装下全部落盘行（默认 10000 会驱逐最旧段）
        ZzLogEngine engine(config);
        QVERIFY(engine.open());
        ZzScrollbackBridge bridge(&term, &engine);

        for (int block = 0; block < 110; ++block) {
            QByteArray chunk;
            for (int i = 0; i < 100; ++i)
                chunk += QStringLiteral("L%1\r\n").arg(block * 100 + i).toUtf8();
            term.recvData(chunk);
        }
        const auto &hv = term.terminal().historyView();
        QCOMPARE(hv.lineCount(), std::size_t(10000));
        const std::uint64_t droppedBefore = hv.droppedLineCount();
        QCOMPARE(droppedBefore, std::uint64_t(998)); // 末行 \r\n 再挤出 1 行
        QCOMPARE(engine.totalLines(), 10998ULL);

        // 到顶后仍上滚：触发读回注入一块（默认 200 行）
        term.setScrollOffset(term.maxScrollOffset());
        QWheelEvent up(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0),
                       QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(&term, &up);

        // 注入行进入最旧端；减法口径基线回退 200
        QCOMPARE(hv.lineCount(), std::size_t(10200));
        QCOMPARE(hv.droppedLineCount(), droppedBefore - 200);
        // 读回内容 = 引擎中紧邻 Core 旧最旧端之前的 200 行（旧→新，纯文本）：
        // 引擎行 [798, 998) = L798..L997，注入后历史行 0 即 L798
        QCOMPARE(historyLineText(hv, 0), QStringLiteral("L798"));
        QCOMPARE(historyLineText(hv, 199), QStringLiteral("L997"));
        QCOMPARE(historyLineText(hv, 200), QStringLiteral("L998")); // 旧最旧端顺移

        // 视口稳定：注入只涨最旧端，离底偏移数值不因注入跳动；
        // 本次滚轮的 3 行步进落在刚注入的区段（继续上滚的起点）
        QCOMPARE(term.scrollOffset(), 10003);
        // 可见首行 = 历史行 10200-10003 = 197 → 引擎行 798+197 = L995
        QCOMPARE(term.visibleLineTextForTest(0), QStringLiteral("L995"));

        // 再次越顶：注入下一块（引擎行 [598, 798)）
        term.setScrollOffset(term.maxScrollOffset());
        QWheelEvent up2(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0),
                        QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                        Qt::NoScrollPhase, false);
        QApplication::sendEvent(&term, &up2);
        QCOMPARE(hv.lineCount(), std::size_t(10400));
        QCOMPARE(hv.droppedLineCount(), droppedBefore - 400);
        QCOMPARE(historyLineText(hv, 0), QStringLiteral("L598"));

        // 追加对账不受注入扰动：再喂新行，引擎只追加真实 scrollout
        //（屏上既有行 L10998/L10999 被新行挤出落盘，各一次）
        const quint64 engineBefore = engine.totalLines();
        term.recvData(QByteArray("tail-1\r\ntail-2\r\n"));
        QCOMPARE(engine.totalLines(), engineBefore + 2);
        const QVector<ZzLogLine> tail =
            engine.getLines(engine.totalLines() - 2, 2);
        QCOMPARE(tail.at(0).text, QStringLiteral("L10998"));
        QCOMPARE(tail.at(1).text, QStringLiteral("L10999"));
    }

    /**
     * @brief reflow 对账复位（Z2 任务 5）：变列重组使历史行身份重建
     *       （generation 变、dropped 不计重组），差分游标回锚到 newest，
     *        重组前已落盘内容不重复追加，重组后新 scrollout 正常续走。
     */
    void reflowResetsAppendCursor()
    {
        ZzCoreTerminalWidget term;
        term.terminal().resize(10, 4);
        ZzLogEngine engine(ZzLogEngine::Config{});
        QVERIFY(engine.open());
        ZzScrollbackBridge bridge(&term, &engine);

        // 8 条 12 字符行：10 列每条折 2 物理行 → 16 行 + 末行 \r\n 再挤
        // 1 行 → 历史 13，屏 4（含空白光标行）
        for (int i = 0; i < 8; ++i)
            term.recvData(QStringLiteral("wrap%1-xxxxxxx\r\n").arg(i).toUtf8());
        QCOMPARE(engine.totalLines(), 13ULL);
        const QVector<ZzLogLine> prefixBefore = engine.getLines(0, 13);

        // 变宽 reflow：10 → 40 列，折链拼合且屏从历史底部回补，
        // 历史 13 → 5（行身份重建，dropped 不计重组）
        term.terminal().resize(40, 4);
        QCOMPARE(term.terminal().historyView().lineCount(), std::size_t(5));

        // reflow 后首个 feed：游标回锚（本 feed 滚出的行按旧形态已在盘上，
        // 属复位边界不追加）；后续 feed 真实 scrollout 正常续走
        term.recvData(QByteArray("new-1\r\nnew-2\r\n"));
        QCOMPARE(engine.totalLines(), 13ULL); // 回锚：不重复追加重组前历史
        term.recvData(QByteArray("new-3\r\nnew-4\r\nnew-5\r\n"));
        QCOMPARE(engine.totalLines(), 16ULL); // 续走：wrap7/new-1/new-2 各一次

        // 重组前落盘前缀原样保留（无重复、无重写）
        QCOMPARE(logTexts(engine.getLines(0, 13)), logTexts(prefixBefore));
        // 新追加段：重组后滚出的旧屏行 + 新行，各一次
        const QVector<ZzLogLine> appended = engine.getLines(13, 3);
        QCOMPARE(appended.at(0).text, QStringLiteral("wrap7-xxxxxxx"));
        QCOMPARE(appended.at(1).text, QStringLiteral("new-1"));
        QCOMPARE(appended.at(2).text, QStringLiteral("new-2"));
    }

private:
    /** @brief 历史第 idx 行纯文本（去尾部填充空格）。 */
    static QString historyLineText(const ZzHistoryView &hv, std::size_t idx)
    {
        const ZzLineView line = hv.lineAt(idx);
        QString text;
        for (int c = 0; c < line.cellCount(); ++c)
            text += QString::fromStdString(line.cellAt(c).text);
        while (text.endsWith(QLatin1Char(' ')))
            text.chop(1);
        return text;
    }

    /** @brief 引擎行段的纯文本列表（ZzLogLine 无 operator==，逐文本比）。 */
    static QStringList logTexts(const QVector<ZzLogLine> &lines)
    {
        QStringList out;
        out.reserve(lines.size());
        for (const ZzLogLine &line : lines)
            out.append(line.text);
        return out;
    }
};

QTEST_MAIN(tst_ZzScrollbackBridge)
#include "tst_ZzScrollbackBridge.moc"
