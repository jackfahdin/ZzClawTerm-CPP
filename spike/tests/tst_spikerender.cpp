/**
 * @file tst_spikerender.cpp
 * @brief M13 spike QTest（offscreen）：feed->renderView 与 PTY 全链验证。
 *
 * - case 1（无 PTY 依赖）：feedForTest 喂标记串，断言 renderView 文本含标记；
 * - case 2（PTY 链路）：ZzPty spawn cat，writeAll 写标记，cat 回显经
 *   QSocketNotifier->feedForTest 全链上屏，QTRY_VERIFY_WITH_TIMEOUT(5s) 断言。
 * - case 3（M14）：滚动偏移拼接——上滚后可见行为历史行，回底恢复屏幕行；
 * - case 4（M14）：Alternate 屏历史归零，滚动偏移夹取到 0。
 */

#include <QSocketNotifier>
#include <QtTest>

#include <fcntl.h>
#include <span>
#include <string>

#include "../ZzCoreViewWidget.h"
#include "ZzPty.h"

namespace {

/**
 * @brief 汇聚 renderView 全部屏幕行文本（行尾空白不修剪，便于子串断言）。
 * @param term 终端对象。
 * @return 各行文本以换行拼接的 QString。
 */
[[nodiscard]] QString screenText(const ZzTerminal& term)
{
    const ZzRenderView& view = term.renderView();
    const ZzSize      grid   = view.size();
    QString           out;
    for (int row = 0; row < grid.rows; ++row) {
        const ZzLineView line = view.lineAt(row);
        for (int col = 0; col < grid.cols; ++col)
            out += QString::fromStdString(line.cellAt(col).text);
        out += QLatin1Char('\n');
    }
    return out;
}

} // namespace

/**
 * @brief spike 渲染链路测试套件。
 */
class TstSpikeRender : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief case 1：直接 feed 标记串，renderView 文本应含标记。
     */
    void feedRendersMark();

    /**
     * @brief case 2：PTY(spawn cat) 回显标记经 feedForTest 全链上屏。
     */
    void ptyChainRendersMark();

    /**
     * @brief case 3（M14）：滚动偏移拼接——上滚后可见行为历史行，回底恢复屏幕行。
     */
    void scrollOffsetComposesHistory();

    /**
     * @brief case 4（M14）：Alternate 屏历史归零，滚动偏移夹取到 0。
     */
    void alternateClampsScroll();
};

void TstSpikeRender::feedRendersMark()
{
    ZzCoreViewWidget widget;
    widget.feedForTest("echo zz-spike-mark\r\n");
    QVERIFY(screenText(widget.terminal()).contains(QStringLiteral("zz-spike-mark")));
}

void TstSpikeRender::ptyChainRendersMark()
{
    ZzCoreViewWidget widget;

    auto pty = ZzPty::spawn(ZzPtyConfig{{"cat"}, 80, 24, true});
    QVERIFY(pty != nullptr);

    // ZzPty 的 master fd 默认阻塞：read 循环到 EAGAIN 需调用方自置 O_NONBLOCK。
    fcntl(pty->masterFd(), F_SETFL, O_NONBLOCK);

    QSocketNotifier notifier(pty->masterFd(), QSocketNotifier::Read, &widget);
    QObject::connect(&notifier, &QSocketNotifier::activated, &widget, [&] {
        std::byte buf[64 * 1024];
        for (;;) {
            const std::ptrdiff_t n = pty->read(std::span<std::byte>(buf, sizeof(buf)));
            if (n <= 0)
                break;
            widget.feedForTest(
                std::string_view(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n)));
        }
    });

    const std::string mark = "zz-spike-pty-mark\r\n";
    QVERIFY(pty->writeAll(std::as_bytes(std::span<const char>(mark.data(), mark.size()))));

    QTRY_VERIFY_WITH_TIMEOUT(
        screenText(widget.terminal()).contains(QStringLiteral("zz-spike-pty-mark")), 5000);
}

void TstSpikeRender::scrollOffsetComposesHistory()
{
    ZzCoreViewWidget widget; // 80x24，历史容量 10000
    std::string script;
    for (int i = 0; i < 40; ++i)
        script += "hist-" + std::to_string(i) + "\r\n";
    widget.feedForTest(script);

    // 24 行屏幕喂 40 个换行：滚出 17 行进历史（hist-0 .. hist-16）。
    QCOMPARE(widget.maxScrollOffset(), 17);

    // 上滚到底：视口首行 = 最旧历史行。
    widget.setScrollOffset(widget.maxScrollOffset());
    QCOMPARE(widget.scrollOffset(), 17);
    QVERIFY(widget.visibleLineTextForTest(0).contains(QStringLiteral("hist-0")));

    // 回底恢复：屏幕末行上方为最新内容行 hist-39。
    widget.setScrollOffset(0);
    QCOMPARE(widget.scrollOffset(), 0);
    const int rows = widget.terminal().size().rows;
    QVERIFY(widget.visibleLineTextForTest(rows - 2).contains(QStringLiteral("hist-39")));

    // 越界夹取。
    widget.setScrollOffset(9999);
    QCOMPARE(widget.scrollOffset(), 17);
}

void TstSpikeRender::alternateClampsScroll()
{
    ZzCoreViewWidget widget;
    std::string script;
    for (int i = 0; i < 40; ++i)
        script += "hist-" + std::to_string(i) + "\r\n";
    widget.feedForTest(script);
    QVERIFY(widget.maxScrollOffset() > 0);

    widget.feedForTest("\x1b[?1049h"); // 进 Alternate：契约 lineCount 恒 0
    QCOMPARE(widget.maxScrollOffset(), 0);
    widget.setScrollOffset(5);
    QCOMPARE(widget.scrollOffset(), 0); // 夹取到 0

    widget.feedForTest("\x1b[?1049l"); // 回 Primary 恢复
    QCOMPARE(widget.maxScrollOffset(), 17);
}

QTEST_MAIN(TstSpikeRender)
#include "tst_spikerender.moc"
