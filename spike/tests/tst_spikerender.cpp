/**
 * @file tst_spikerender.cpp
 * @brief M13 spike QTest（offscreen）：feed->renderView 与 PTY 全链验证。
 *
 * - case 1（无 PTY 依赖）：feedForTest 喂标记串，断言 renderView 文本含标记；
 * - case 2（PTY 链路）：ZzPty spawn cat，writeAll 写标记，cat 回显经
 *   QSocketNotifier->feedForTest 全链上屏，QTRY_VERIFY_WITH_TIMEOUT(5s) 断言。
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

QTEST_MAIN(TstSpikeRender)
#include "tst_spikerender.moc"
