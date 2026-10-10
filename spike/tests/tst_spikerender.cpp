/**
 * @file tst_spikerender.cpp
 * @brief M13 spike QTest（offscreen）：feed->renderView 与 PTY 全链验证。
 *
 * - case 1（无 PTY 依赖）：feedForTest 喂标记串，断言 renderView 文本含标记；
 * - case 2（PTY 链路）：ZzPty spawn cat，writeAll 写标记，cat 回显经
 *   QSocketNotifier->feedForTest 全链上屏，QTRY_VERIFY_WITH_TIMEOUT(5s) 断言。
 * - case 3（M14）：滚动偏移拼接——上滚后可见行为历史行，回底恢复屏幕行；
 * - case 4（M14）：Alternate 屏历史归零，滚动偏移夹取到 0。
 * - case 5（M14 R1）：滚轮方向——上滚（steps>0）进入历史，下滚回底。
 * - case 6（M13 修复回归）：spawn bash（rawMode=false）交互回显链——
 *   键入命令回显上屏并产生独立输出行。
 * - case 7（M17b）：不换行模式——拼接行渲染、横向滚动窗口、选区复制完整长行。
 */

#include <QSocketNotifier>
#include <QtTest>

#include <fcntl.h>
#include <span>
#include <string>

#include "../ZzCoreViewWidget.h"
#include "ZzPty.h"

#include <ZzTerm/UnwrapView.h>

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

    /**
     * @brief case 5（M14 R1）：滚轮方向——上滚（steps>0）进入历史，下滚回底。
     */
    void wheelUpScrollsIntoHistory();

    /**
     * @brief case 6（M13 修复回归）：交互 bash 链回显——rawMode=false 下
     * 键入命令应回显上屏且产生独立输出行（rawMode=true 的 -echo 下必失败）。
     */
    void bashEchoChainWorks();

    /**
     * @brief case 7（M17b）：不换行模式——拼接行渲染 + 横向滚动 + 选区复制完整长行。
     */
    void unwrapMode();
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

void TstSpikeRender::wheelUpScrollsIntoHistory()
{
    ZzCoreViewWidget widget;
    std::string script;
    for (int i = 0; i < 40; ++i)
        script += "hist-" + std::to_string(i) + "\r\n";
    widget.feedForTest(script);
    QCOMPARE(widget.scrollOffset(), 0);

    // 上滚一格（angleDelta().y()=+120 -> steps=+1）：offset 增大 3 行进入历史。
    widget.scrollByWheelStepsForTest(1);
    QCOMPARE(widget.scrollOffset(), 3);
    QVERIFY(widget.visibleLineTextForTest(0).contains(QStringLiteral("hist-14")));

    // 下滚一格（steps=-1）：回底。
    widget.scrollByWheelStepsForTest(-1);
    QCOMPARE(widget.scrollOffset(), 0);
}

void TstSpikeRender::bashEchoChainWorks()
{
    ZzCoreViewWidget widget;

    // 与 main.cpp runLocal 的 spawn 配置保持一致（rawMode=false 交互档）——
    // main.cpp 该配置若变更，此处须同步修改，本用例即该配置的回归护栏。
    auto pty = ZzPty::spawn(ZzPtyConfig{{"bash"}, 80, 24, false});
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

    // bash 启动与 readline 就绪需要时间：等提示符上屏后再写入命令。
    QTRY_VERIFY_WITH_TIMEOUT(screenText(widget.terminal()).contains(QRegularExpression("[$#]")),
                             5000);

    const std::string cmd = "echo zz-echo-mark\r";
    QVERIFY(pty->writeAll(std::as_bytes(std::span<const char>(cmd.data(), cmd.size()))));

    // 回显上屏（命令行含 echo zz-echo-mark）+ 独立输出行（裸 zz-echo-mark），
    // 两处共出现 >= 2 次；rawMode=true 下 -echo 无回显字节，本断言必失败。
    QTRY_VERIFY_WITH_TIMEOUT(
        screenText(widget.terminal()).count(QStringLiteral("zz-echo-mark")) >= 2, 5000);
}

// M17b：不换行模式——拼接行渲染 + 横向滚动 + 选区复制完整长行。
void TstSpikeRender::unwrapMode()
{
    ZzCoreViewWidget widget;
    // 造一条 25 格长行（10 列屏折 3 行）+ 一行短行。
    // 注：4 行屏喂完末尾 \r\n 会滚出一行入历史（x 链跨缝），拼接空间共
    // [x 链, b, 空] 3 条（对齐 ZzTermCore testStitchAcrossHistorySeam 口径），
    // 因此钉底视口顶 viewTop = 3-4 = -1，可见行 0 顶部留白、行 1 才是 x 链。
    widget.terminal().resize(10, 4);
    widget.feedForTest("xxxxxxxxxxxxxxxxxxxxxxxxx\r\nb\r\n");
    QCOMPARE(widget.terminal().unwrapView().lineCount(), std::size_t(3));
    // OFF：物理行渲染（现状）——可见行 0 = 滚上后的 x 链第 2 段（10 x）
    QCOMPARE(widget.visibleLineTextForTest(0), QString("xxxxxxxxxx"));
    // ON：拼接行渲染。拼接证据：5 物理行（1 历史 + 4 屏幕）拼成 3 条，
    // 且横向可滚上限 15 = maxCellCount(25) - cols(10)（OFF 下恒 0）。
    widget.setUnwrapMode(true);
    QCOMPARE(widget.maxHScrollOffset(), 25 - 10);
    QCOMPARE(widget.visibleStitchedCountForTest(), 3);
    QCOMPARE(widget.visibleStitchedTextForTest(0), QString()); // 顶部留白
    // 可见行 1 = x 链；文本钩子口径为可见窗口 [hOffset, hOffset+cols)，
    // 10 列屏单次最多 10 格，全长由下方 maxHScrollOffset 与 selectedText 钉住。
    QCOMPARE(widget.visibleStitchedTextForTest(1), QString(10, 'x'));
    // 横向滚动：窗口 [10, 20)
    widget.setHScrollOffset(10);
    QCOMPARE(widget.hScrollOffset(), 10);
    QCOMPARE(widget.visibleStitchedTextForTest(1), QString(10, 'x'));
    // 越界夹取到 maxHScrollOffset
    widget.setHScrollOffset(999);
    QCOMPARE(widget.hScrollOffset(), 15);
    widget.setHScrollOffset(0);
    // 选区复制完整长行（含屏外）。
    // 坐标空间勘定：setSelection 取逻辑行坐标（折链合并；Core
    // zzLogicalLineCount 口径），而 ZzUnwrapView::fromStitched 输出物理统一
    // 坐标，两者仅在无折链时相等——此处 x 链占物理行 0..2，fromStitched(0,24)
    // 回映物理 {2,4}；逻辑空间中拼接行 0 == 逻辑行 0，[0,25) 即整条 25 x。
    const auto& view = widget.terminal().unwrapView();
    QVERIFY((view.fromStitched(0, 0) == ZzLogicalPos{0, 0}));
    QVERIFY((view.fromStitched(0, 24) == ZzLogicalPos{2, 4}));
    widget.terminal().setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 25});
    QCOMPARE(QString::fromStdString(widget.terminal().selectedText()),
             QString(25, 'x'));
    // 切回 OFF：横向归零、物理渲染还原
    widget.setUnwrapMode(false);
    QCOMPARE(widget.hScrollOffset(), 0);
    QCOMPARE(widget.maxHScrollOffset(), 0);
    QCOMPARE(widget.visibleLineTextForTest(0), QString("xxxxxxxxxx"));
}

QTEST_MAIN(TstSpikeRender)
#include "tst_spikerender.moc"
