#include <QtTest/QtTest>

#include <QtCore/QStringConverter>
#include <QtCore/QTemporaryDir>

#include "ZzMockTransport.h"
#include "settings/ZzAppSettings.h"
#include "terminal/ZzCoreTerminalWidget.h"
#include "terminal/ZzScrollbackBridge.h"
#include "terminal/ZzTerminalView.h"
#include "transport/ZzTransportRegistry.h"

/**
 * @brief Z1 换血后终端视图胶水契约：双向字节流（含转码）、尺寸去抖
 * 转发、设置应用、状态透传、滚动历史降级提示与温层唯一性。
 */
class tst_ZzTerminalView : public QObject
{
    Q_OBJECT
private slots:
    void init() { qRegisterMetaType<ZzTransportInterface::State>(); }

    void bidirectionalByteStream()
    {
        ZzTerminalView view;
        auto *transport = new ZzMockTransport(&view);
        transport->echoEnabled = false;
        view.setTransport(transport);

        ZzTransportEndpoint endpoint;
        transport->open(endpoint);
        QTRY_VERIFY(transport->state() == ZzTransportInterface::State::Connected);

        // 远端 → 终端：feed 后渲染文本可见
        transport->simulateData("hello remote\r\n");
        QCoreApplication::processEvents();
        QCOMPARE(view.termWidget()->visibleLineTextForTest(0),
                 QString("hello remote"));

        // 终端 → 远端：键盘经 sendData → 转码（UTF-8 直通）→ write
        QTest::keyClicks(view.termWidget(), "ls");
        QTest::keyClick(view.termWidget(), Qt::Key_Return);
        QCoreApplication::processEvents();
        QCOMPARE(transport->writtenData, QByteArray("ls\r"));
    }

    void gbkTranscodingRoundTrip()
    {
        QTemporaryDir dir; // 断言失败栈展开亦清理
        QVERIFY(dir.isValid());
        ZzAppSettings settings(dir.filePath(QStringLiteral("gbk.ini")));
        settings.setEncoding(QStringLiteral("GBK"));

        ZzTerminalView view;
        view.applySettings(settings);
        QCOMPARE(view.encoding(), QStringLiteral("GBK"));
        auto *transport = new ZzMockTransport(&view);
        transport->echoEnabled = false;
        view.setTransport(transport);
        transport->open(ZzTransportEndpoint{});
        QTRY_VERIFY(transport->state() == ZzTransportInterface::State::Connected);

        // 收：GBK 字节「中」（0xD6 0xD0）→ UTF-8 feed → 渲染为「中」
        transport->simulateData(QByteArray("\xD6\xD0\r\n", 4));
        QCoreApplication::processEvents();
        QVERIFY(view.termWidget()->visibleLineTextForTest(0)
                    .startsWith(QStringLiteral("中")));
    }

    void sizeForwardsToTransport()
    {
        ZzTerminalView view;
        auto *transport = new ZzMockTransport(&view);
        view.setTransport(transport);
        QSignalSpy sizeSpy(&view, &ZzTerminalView::sizeChanged);

        emit view.termWidget()->gridResized(100, 40);
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.first().at(0).toInt(), 100);
        QCOMPARE(transport->resizeCallCount, 0); // 去抖窗口内不转发
        QTRY_COMPARE_WITH_TIMEOUT(transport->resizeCallCount, 1, 1000);
        QCOMPARE(transport->lastCols, 100);
        QCOMPARE(transport->lastRows, 40);
    }

    void sizeStormCoalescesToFinal()
    {
        ZzTerminalView view;
        auto *transport = new ZzMockTransport(&view);
        view.setTransport(transport);
        for (int i = 0; i < 10; ++i) {
            emit view.termWidget()->gridResized(100 + i, 40);
            QTest::qWait(20);
        }
        QTRY_COMPARE_WITH_TIMEOUT(transport->resizeCallCount, 1, 1000);
        QCOMPARE(transport->lastCols, 109);
        QCOMPARE(transport->lastRows, 40);
    }

    void statePassthrough()
    {
        ZzTerminalView view;
        auto *transport = new ZzMockTransport(&view);
        view.setTransport(transport);
        QSignalSpy stateSpy(&view, &ZzTerminalView::stateChanged);
        transport->open(ZzTransportEndpoint{});
        QTRY_COMPARE(view.transportState(), ZzTransportInterface::State::Connected);
        QCOMPARE(stateSpy.count(), 2);
        transport->simulateDisconnect(QStringLiteral("对端关闭"));
        QCOMPARE(view.transportState(), ZzTransportInterface::State::Disconnected);
    }

    /**
     * @brief 回归：enableScrollback 时温层 open 失败（同步降级）必须提示用户（规格 §八）。
     *        曾因 open() 先于桥/提示链路接线而丢失该信号。
     *        冷层接线后目录只读会让冷层与温层 open 双双失败：先报冷层降级
     *        （degradedToWarmOnly），再报温层降级（degradedToMemoryOnly，
     *        最终状态为纯内存模式，末条提示为准）。
     */
    void scrollbackDegradationPromptsOnOpen()
    {
#ifdef Q_OS_WIN
        // 用例靠「只读目录使温层文件创建失败」触发降级；Windows 无 POSIX 目录
        // 权限位（QFile::setPermissions 只映射只读属性，不阻止目录写入），
        // 触发装置无效，降级链路由 Linux/macOS CI 覆盖
        QSKIP("只读目录触发装置依赖 POSIX 权限语义，Windows 不适用");
#endif
        QStandardPaths::setTestModeEnabled(true); // 隔离用户真实配置目录
        // 温层文件名带每标签唯一后缀，无法预知精确路径；改为把 scrollback
        // 目录置为只读，任何温层文件创建都会失败 → open() 同步降级
        const QString dirPath =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/scrollback");
        QVERIFY(QDir().mkpath(dirPath));
        QVERIFY(QFile::setPermissions(
            dirPath, QFileDevice::ReadOwner | QFileDevice::ExeOwner));

        ZzTerminalView view;
        QSignalSpy spy(&view, &ZzTerminalView::errorOccurred);
        view.enableScrollback(QStringLiteral("test-degrade-open"));
        QCOMPARE(spy.count(), 2); // 冷层降级 + 温层降级各一条
        // 断言 tr 同键前缀（%1 前的外壳文案），随界面语言切换不失效
        QVERIFY(spy.at(0).at(0).toString().contains(
            ZzTerminalView::tr("滚动历史已降级为温层模式：%1").arg(QString())));
        QVERIFY(spy.at(1).at(0).toString().contains(
            ZzTerminalView::tr("滚动历史已降级为内存模式：%1").arg(QString())));

        // 恢复写权限，避免影响同进程后续用例
        QVERIFY(QFile::setPermissions(
            dirPath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner
                | QFileDevice::ExeOwner));
    }

    /**
     * @brief 回归：同 sessionId 双开标签时温层文件必须每标签唯一。
     *        曾共用 <sessionId>.warm，后开标签的 QFile::remove 会截断
     *        先开标签正在使用的温层。
     */
    void scrollbackWarmFilesAreUniquePerTab()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString sessionId = QStringLiteral("test-double-open");
        const QString dirPath =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/scrollback");
        // 清掉历史残留，保证计数断言确定
        for (const QString &file :
             QDir(dirPath).entryList({sessionId + QStringLiteral("-*.warm")},
                                     QDir::Files)) {
            QFile::remove(dirPath + QLatin1Char('/') + file);
        }

        ZzTerminalView view1;
        view1.enableScrollback(sessionId);
        ZzTerminalView view2; // 同 sessionId 同时存活（同 profile 双开）
        view2.enableScrollback(sessionId);

        const QStringList warmFiles =
            QDir(dirPath).entryList({sessionId + QStringLiteral("-*.warm")},
                                    QDir::Files);
        QCOMPARE(warmFiles.size(), 2);
    }

    /**
     * @brief 会话级编码覆盖全局值，且不被后续 applySettings 冲掉；
     *        置回空串后跟随全局（ZzTabManager 开会话时按 profile.encoding 接通）。
     */
    void sessionEncodingOverridesGlobal()
    {
        QTemporaryDir dir; // 断言失败栈展开亦清理
        QVERIFY(dir.isValid());
        ZzAppSettings settings(dir.filePath(QStringLiteral("session-encoding.ini"))); // 全局默认 UTF-8

        ZzTerminalView view;
        view.setSessionEncoding(QStringLiteral("GBK"));
        QCOMPARE(view.encoding(), QStringLiteral("GBK"));

        view.applySettings(settings); // 全局变更不覆盖会话级编码
        QCOMPARE(view.encoding(), QStringLiteral("GBK"));

        view.setSessionEncoding(QString()); // 空串 = 跟随全局
        QCOMPARE(view.encoding(), ZzAppSettings::instance().encoding());
    }

    /**
     * @brief 会话级配色覆盖全局值，且不被后续 applySettings 冲掉；
     *        置回空串后跟随全局（ZzTabManager 开会话时按 profile.colorSchemeName 接通）。
     */
    void sessionColorSchemeOverridesGlobal()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ZzAppSettings settings(dir.filePath(QStringLiteral("session-scheme.ini")));

        ZzTerminalView view;
        view.setSessionColorScheme(QStringLiteral("Dracula"));
        QCOMPARE(view.termWidget()->colorSchemeName(), QStringLiteral("Dracula"));

        settings.setColorScheme(QStringLiteral("iTerm2 Solarized Light"));
        view.applySettings(settings); // 全局变更不覆盖会话级配色
        QCOMPARE(view.termWidget()->colorSchemeName(), QStringLiteral("Dracula"));

        view.setSessionColorScheme(QString()); // 空串 = 跟随全局
        QCOMPARE(view.termWidget()->colorSchemeName(),
                 ZzAppSettings::instance().colorScheme());
    }

    void applySettingsPropagatesColorScheme()
    {
        QTemporaryDir dir; // 断言失败栈展开亦清理
        QVERIFY(dir.isValid());
        ZzTerminalView view;
        ZzAppSettings settings(dir.filePath(QStringLiteral("colorscheme.ini")));
        settings.setColorScheme(QStringLiteral("Dracula"));
        view.applySettings(settings);
        QCOMPARE(view.termWidget()->colorSchemeName(), QStringLiteral("Dracula"));
    }
};

QTEST_MAIN(tst_ZzTerminalView)
#include "tst_ZzTerminalView.moc"
