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
 * appendLine。变列 reflow（行身份重建）的对账复位由 gridResized 驱动，
 * 变宽/变窄双向钉死（reflowResetsAppendCursor / narrowReflowDoesNotDuplicateLog
 * / resizeWithBurstOutputLosesNoLines，Z2 任务 5 + 审查修复收口）。
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
     * @brief reflow 对账复位（Z2 任务 5 + 审查修复）：变列重组使历史行身份
     *        重建（dropped 不计重组），复位在 gridResized 精确发生（Core 同步
     *        reflow 完成后才发射），重组前已落盘内容不重复追加，reflow 后
     *        首个 feed 的真实 scrollout 不被吞。
     */
    void reflowResetsAppendCursor()
    {
        ZzCoreTerminalWidget term;
        term.show(); // 隐藏 widget 的 resize() 不投递 QResizeEvent（Qt 延迟到 show）
        const int cw = term.cellWidthForTest();
        const int ch = term.cellHeightForTest();
        // 最小网格钳制（24×6）后 widget 路径最小网格即 24 列 × 6 行；
        // 26 字符行在 24 列折 2 物理行、在 40 列不折，保住折链拼合场景
        const QString pad(20, QLatin1Char('x')); // "wrapN-" + 20 = 26 字符
        term.resize(24 * cw, 6 * ch); // resizeEvent 链路：term 24x6
        ZzLogEngine engine(ZzLogEngine::Config{});
        QVERIFY(engine.open());
        ZzScrollbackBridge bridge(&term, &engine);

        // 8 条 26 字符行：24 列每条折 2 物理行 → 16 行 + 末行 \r\n 再挤
        // 1 行 → 历史 11，屏 6（含空白光标行）
        for (int i = 0; i < 8; ++i)
            term.recvData(QStringLiteral("wrap%1-%2\r\n").arg(i).arg(pad).toUtf8());
        QCOMPARE(engine.totalLines(), 11ULL);
        const QVector<ZzLogLine> prefixBefore = engine.getLines(0, 11);

        // 变宽 reflow：24 → 40 列，折链拼合且屏从历史底部回补，
        // 历史 11 → 3（行身份重建，dropped 不计重组）；gridResized 发射时
        // 复位已完成（游标 = 新 newest = 3），不依赖下一个 feed
        term.resize(40 * cw, 6 * ch);
        QCOMPARE(term.terminal().historyView().lineCount(), std::size_t(3));

        // reflow 后首个 feed：滚出的重组屏上行正常落盘（审查修复②：不再
        // 被回锚吞掉）；6 行屏喂足 5+3 行让新行也滚出，验证新行落盘不丢。
        // 屏上重组行以新形态（拼合行）落盘属边界既知形态——其旧形态
        //（折行对）仍在前缀中保留
        term.recvData(QByteArray("new-1\r\nnew-2\r\nnew-3\r\nnew-4\r\nnew-5\r\n"));
        QCOMPARE(engine.totalLines(), 16ULL);
        term.recvData(QByteArray("new-6\r\nnew-7\r\nnew-8\r\n"));
        QCOMPARE(engine.totalLines(), 19ULL);

        // 重组前落盘前缀原样保留（无重写）
        QCOMPARE(logTexts(engine.getLines(0, 11)), logTexts(prefixBefore));
        // 新追加段：重组后滚出的屏上行 + 新行，各一次
        const QVector<ZzLogLine> appended = engine.getLines(11, 8);
        QCOMPARE(appended.at(0).text, QStringLiteral("wrap3-%1").arg(pad));
        QCOMPARE(appended.at(1).text, QStringLiteral("wrap4-%1").arg(pad));
        QCOMPARE(appended.at(2).text, QStringLiteral("wrap5-%1").arg(pad));
        QCOMPARE(appended.at(3).text, QStringLiteral("wrap6-%1").arg(pad));
        QCOMPARE(appended.at(4).text, QStringLiteral("wrap7-%1").arg(pad));
        QCOMPARE(appended.at(5).text, QStringLiteral("new-1"));
        QCOMPARE(appended.at(6).text, QStringLiteral("new-2"));
        QCOMPARE(appended.at(7).text, QStringLiteral("new-3"));
    }

    /**
     * @brief 审查修复①：缩列 reflow 行数膨胀（newest > 游标）同样是行身份
     *        重建——重组新形态不得当作新行重复落盘，只有真实 scrollout 追加。
     */
    void narrowReflowDoesNotDuplicateLog()
    {
        ZzCoreTerminalWidget term;
        term.show();
        const int cw = term.cellWidthForTest();
        const int ch = term.cellHeightForTest();
        term.resize(40 * cw, 6 * ch); // 40 列：30 字符行不折（最小网格钳制后行下限 6）
        ZzLogEngine engine(ZzLogEngine::Config{});
        QVERIFY(engine.open());
        ZzScrollbackBridge bridge(&term, &engine);

        // 8 条 30 字符行：40 列每条 1 物理行，+ 末行 \r\n 再挤 1 行 → 历史 3
        for (int i = 0; i < 8; ++i)
            term.recvData(
                QStringLiteral("segment-%1-abcdefghij0123456789\r\n").arg(i).toUtf8());
        QCOMPARE(engine.totalLines(), 3ULL);

        // 缩列 reflow：40 → 24 列（钳制下限），每条折 2 物理行，行数膨胀（行身份重建）
        term.resize(24 * cw, 6 * ch);
        const std::size_t histAfterReflow =
            term.terminal().historyView().lineCount();
        QVERIFY(histAfterReflow > 3); // 前提：缩列使历史行数增长

        // 复位已在 gridResized 完成：reflow 后 feed 只追加真实 scrollout，
        // 膨胀出的重组行一行都不重复落盘（自校准：滚动数取 Core 实测增量）
        const quint64 engineBefore = engine.totalLines();
        term.recvData(QByteArray("na\r\nnb\r\nnc\r\n"));
        const auto &hv = term.terminal().historyView();
        const quint64 scrolled =
            quint64(hv.lineCount()) - quint64(histAfterReflow); // 无裁剪（dropped=0）
        QVERIFY(scrolled > 0);
        QCOMPARE(engine.totalLines(), engineBefore + scrolled);
        // 旧形态行仍恰好一次（重组新形态未混入持久层）
        QCOMPARE(logTexts(engine.getLines(0, engine.totalLines()))
                     .count(QStringLiteral("segment-0-abcdefghij0123456789")),
                 1);
    }

    /**
     * @brief 审查修复②：resize 同帧突发输出不丢行——复位若在 contentFed
     *        （双向回锚），reflow 后首个大 feed 的真实新行会被抬游标吞掉；
     *        复位在 gridResized 则突发输出的 scrollout 逐行落盘。
     */
    void resizeWithBurstOutputLosesNoLines()
    {
        ZzCoreTerminalWidget term;
        term.show();
        const int cw = term.cellWidthForTest();
        const int ch = term.cellHeightForTest();
        term.resize(40 * cw, 6 * ch); // 最小网格钳制后行下限 6
        ZzLogEngine engine(ZzLogEngine::Config{});
        QVERIFY(engine.open());
        ZzScrollbackBridge bridge(&term, &engine);

        for (int i = 0; i < 8; ++i)
            term.recvData(
                QStringLiteral("segment-%1-abcdefghij0123456789\r\n").arg(i).toUtf8());
        QCOMPARE(engine.totalLines(), 3ULL); // 8 行 + 末行 \r\n 进 6 行屏

        // 缩列 reflow（40 → 24 列钳制下限）后立刻突发输出（同一帧，无事件循环间歇）
        term.resize(24 * cw, 6 * ch);
        const std::size_t histAfterReflow =
            term.terminal().historyView().lineCount();
        QVERIFY(histAfterReflow > 3);
        const quint64 engineBefore = engine.totalLines();
        QByteArray burst;
        for (int i = 1; i <= 8; ++i)
            burst += QStringLiteral("burst-%1\r\n").arg(i).toUtf8();
        term.recvData(burst);

        // 突发输出的 scrollout 一行不丢：增量恰为 Core 实测滚动数
        const auto &hv = term.terminal().historyView();
        const quint64 scrolled =
            quint64(hv.lineCount()) - quint64(histAfterReflow);
        QVERIFY(scrolled > 0);
        QCOMPARE(engine.totalLines(), engineBefore + scrolled);
        // 且新行确实落盘（burst-1 必滚出：8 行进 6 行屏）
        QVERIFY(logTexts(engine.getLines(engineBefore, scrolled))
                    .contains(QStringLiteral("burst-1")));
        // 重组行不重复落盘（与发现①同口径）
        QCOMPARE(logTexts(engine.getLines(0, engine.totalLines()))
                     .count(QStringLiteral("segment-0-abcdefghij0123456789")),
                 1);
    }

    /**
     * @brief 复审修复 1：ED 3（CSI 3 J 清滚动历史，xterm 语义不动屏幕）后
     *        续行不丢——clear 不复位 totalDropped、列数未变无 gridResized，
     *        差分游标由 contentFed 收缩向兜底回锚（ncurses clear / tmux
     *        clear-history 触发面）。
     */
    void clearScrollbackDoesNotLoseLines()
    {
        ZzCoreTerminalWidget term;
        term.terminal().resize(10, 3);
        ZzLogEngine engine(ZzLogEngine::Config{});
        QVERIFY(engine.open());
        ZzScrollbackBridge bridge(&term, &engine);

        // 8 行进 3 行屏：历史 6（l1..l6 落盘），屏 [l7, l8, 光标行]
        for (int i = 1; i <= 8; ++i)
            term.recvData(QStringLiteral("l%1\r\n").arg(i).toUtf8());
        QCOMPARE(engine.totalLines(), 6ULL);

        // ED 3 清滚动历史（独立 feed：contentFed 此刻历史为空，回锚精确）
        term.recvData(QByteArray("\033[3J"));
        QCOMPARE(term.terminal().historyView().lineCount(), std::size_t(0));

        // 清后分批小量输出：游标不得冻结，新滚出行逐批落盘
        for (int batch = 0; batch < 4; ++batch) {
            term.recvData(QStringLiteral("post-%1\r\npost-%2\r\n")
                              .arg(batch * 2 + 1).arg(batch * 2 + 2).toUtf8());
        }
        // 滚出序：l7、l8（清后首批滚出的屏上旧行，此前未落盘）+ post-1..6
        QCOMPARE(engine.totalLines(), 14ULL);
        QCOMPARE(logTexts(engine.getLines(6, 8)),
                 QStringList({QStringLiteral("l7"), QStringLiteral("l8"),
                              QStringLiteral("post-1"), QStringLiteral("post-2"),
                              QStringLiteral("post-3"), QStringLiteral("post-4"),
                              QStringLiteral("post-5"), QStringLiteral("post-6")}));
    }

    /**
     * @brief 复审修复 2：Alternate 屏活跃期间 resize（vim/htop 里拖窗口），
     *        Core 照样 reflow Primary 历史并发射 gridResized，但
     *        historyView().lineCount() 契约性归零——回锚必须取 buffer 无关
     *        口径（primaryLineCount），否则回主屏后整段历史被当新行重复落盘。
     */
    void resizeInAltScreenDoesNotDuplicate()
    {
        ZzCoreTerminalWidget term;
        term.show();
        const int cw = term.cellWidthForTest();
        const int ch = term.cellHeightForTest();
        term.resize(40 * cw, 6 * ch); // 最小网格钳制后行下限 6
        ZzLogEngine engine(ZzLogEngine::Config{});
        QVERIFY(engine.open());
        ZzScrollbackBridge bridge(&term, &engine);

        // 8 行进 6 行屏：历史 3（line-0..2 落盘），屏 [line-3..7, 光标行]
        for (int i = 0; i < 8; ++i)
            term.recvData(QStringLiteral("line-%1\r\n").arg(i).toUtf8());
        QCOMPARE(engine.totalLines(), 3ULL);

        term.recvData(QByteArray("\033[?1049h")); // 进 Alternate：可见历史归零
        QCOMPARE(term.terminal().historyView().lineCount(), std::size_t(0));
        term.recvData(QByteArray("alt-screen-output\r\n")); // alt 输出不进历史/日志
        QCOMPARE(engine.totalLines(), 3ULL);

        // alt 期间列变：Primary 历史照样 reflow（不可见），gridResized 发射
        term.resize(24 * cw, 6 * ch); // 24 列 = 钳制下限，仍构成 40 → 24 列变

        // 回主屏 + 突发输出：恢复的历史（line-0..2）不得重复落盘，
        // 只有真实 scrollout（屏上行 line-3..7）追加；喂足 5 行使 5 条
        // 屏上旧行全部滚出，保住「每条 line-i 恰好一次」终检
        term.recvData(QByteArray("\033[?1049lpost-a\r\npost-b\r\npost-c\r\npost-d\r\npost-e\r\n"));
        QCOMPARE(engine.totalLines(), 8ULL);
        QCOMPARE(logTexts(engine.getLines(3, 5)),
                 QStringList({QStringLiteral("line-3"), QStringLiteral("line-4"),
                              QStringLiteral("line-5"), QStringLiteral("line-6"),
                              QStringLiteral("line-7")}));
        // 全日志逐条核对：每条 line-i 恰好一次（无重复块）
        const QStringList all = logTexts(engine.getLines(0, engine.totalLines()));
        for (int i = 0; i < 8; ++i)
            QCOMPARE(all.count(QStringLiteral("line-%1").arg(i)), 1);
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
