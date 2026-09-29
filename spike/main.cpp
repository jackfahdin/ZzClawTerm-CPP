/**
 * @file main.cpp
 * @brief M13 spike 弃子宿主：ZzCoreViewWidget + 本地 PTY 链（--local）。
 *
 * --local（默认）：ZzPty spawn bash，QSocketNotifier 监听 masterFd，
 * 读到的字节经 widget.feedForTest 喂入；widget 的键盘编码字节经
 * writeFn_ 转发 pty->writeAll；gridResized 转发 pty->resize。
 *
 * --ssh 属任务 2 范围，本宿主仅识别参数并提示，不实现。
 */

#include <QApplication>
#include <QSocketNotifier>

#include <cstdio>
#include <fcntl.h>
#include <span>
#include <string>

#include "ZzCoreViewWidget.h"
#include "ZzPty.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--ssh"))) {
        std::fprintf(stderr, "spike: --ssh 属任务 2 范围，本宿主未实现。\n");
        return 2;
    }

    ZzCoreViewWidget widget;
    widget.setWindowTitle(QStringLiteral("spike-local"));

    const ZzSize grid = widget.terminal().size();
    auto pty = ZzPty::spawn(ZzPtyConfig{{"bash"}, grid.cols, grid.rows, true});
    if (!pty) {
        std::fprintf(stderr, "spike: ZzPty spawn bash 失败。\n");
        return 1;
    }

    // ZzPty 的 master fd 默认阻塞：read 循环到 EAGAIN 需调用方自置 O_NONBLOCK
    //（QSocketNotifier 已是边界触发可读通知，阻塞 read 会吊死事件循环）。
    fcntl(pty->masterFd(), F_SETFL, O_NONBLOCK);

    // 键盘编码字节 -> PTY。
    widget.setWriteFn([&pty](std::string bytes) {
        pty->writeAll(std::as_bytes(std::span<const char>(bytes.data(), bytes.size())));
    });

    // PTY 输出 -> feed -> 渲染；EOF（子进程退出）即退出程序。
    QSocketNotifier notifier(pty->masterFd(), QSocketNotifier::Read, &widget);
    QObject::connect(&notifier, &QSocketNotifier::activated, &widget, [&] {
        std::byte buf[64 * 1024];
        for (;;) {
            const std::ptrdiff_t n = pty->read(std::span<std::byte>(buf, sizeof(buf)));
            if (n > 0) {
                widget.feedForTest(
                    std::string_view(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n)));
                continue;
            }
            if (n == 0)
                QCoreApplication::quit();
            break; // -1（含 EAGAIN）：本轮读完。
        }
    });

    // 网格尺寸变化 -> PTY window size（SIGWINCH）。
    QObject::connect(&widget, &ZzCoreViewWidget::gridResized, &widget,
                     [&pty](int cols, int rows) { pty->resize(cols, rows); });

    widget.resize(widget.sizeHint());
    widget.show();
    return QCoreApplication::exec();
}
