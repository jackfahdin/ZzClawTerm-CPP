/**
 * @file main.cpp
 * @brief M13 spike 弃子宿主：ZzCoreViewWidget + 本地 PTY 链（--local）/ SSH 链（--ssh）。
 *
 * --local（默认）：ZzPty spawn bash，QSocketNotifier 监听 masterFd，
 * 读到的字节经 widget.feedForTest 喂入；widget 的键盘编码字节经
 * writeFn_ 转发 pty->writeAll；gridResized 转发 pty->resize。
 *
 * --ssh [user@]host[:port]：ZzSshCore 直驱（不经 ZzSshTransport 类）——
 * ZzSshConnection 建连（agent/公钥优先，密码经弹窗兜底；probe 模式直接取消），
 * 主机密钥 spike 简化自动信任（TOFU，指纹打印到 stderr，落临时目录 known_hosts），
 * connected 后 createShellChannel + openShell(xterm-256color)；
 * dataReceived -> feedForTest；writeFn_ -> channel->write；
 * gridResized -> channel->resize（window-change 由库侧去抖转发远端）。
 *
 * --probe：无人值守自检（配合 -platform offscreen）。链路就绪后写
 * "echo zz-spike-probe-ok"，100ms 轮询 renderView 文本含标记即
 * PROBE-OK 退出码 0；15s 超时 PROBE-TIMEOUT 退出码 3。
 */

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QScrollBar>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <fcntl.h>
#include <span>
#include <string>

#include <ZzSshConnection.h>
#include <ZzSshShellChannel.h>

#include "ZzCoreViewWidget.h"
#include "ZzPty.h"

namespace {

/**
 * @brief 汇聚 renderView 全部屏幕行文本（probe 标记断言用，与 QTest 同款）。
 * @param term 终端对象。
 * @return 各行文本以换行拼接的 QString。
 */
[[nodiscard]] QString screenText(const ZzTerminal& term)
{
    const ZzRenderView& view = term.renderView();
    const ZzSize        grid = view.size();
    QString             out;
    for (int row = 0; row < grid.rows; ++row) {
        const ZzLineView line = view.lineAt(row);
        for (int col = 0; col < grid.cols; ++col)
            out += QString::fromStdString(line.cellAt(col).text);
        out += QLatin1Char('\n');
    }
    return out;
}

/**
 * @brief 按 OpenSSH 惯例探测默认私钥（id_ed25519/id_ecdsa/id_rsa 首个存在者）。
 * @return 存在的私钥路径；均无则空串。
 */
[[nodiscard]] QString defaultPrivateKeyPath()
{
    const QString home = QDir::homePath();
    for (const char* name : {"id_ed25519", "id_ecdsa", "id_rsa"}) {
        const QString path = home + QStringLiteral("/.ssh/") + QLatin1String(name);
        if (QFileInfo::exists(path))
            return path;
    }
    return QString();
}

/**
 * @brief 启动 probe 看门狗：轮询标记上屏退出 0，15s 超时退出 3。
 * @param widget 持有终端的 spike widget。
 */
void armProbeWatch(ZzCoreViewWidget& widget)
{
    auto* poll = new QTimer(&widget);
    QObject::connect(poll, &QTimer::timeout, &widget, [&widget] {
        if (screenText(widget.terminal()).contains(QStringLiteral("zz-spike-probe-ok"))) {
            std::fprintf(stderr, "spike: PROBE-OK\n");
            QCoreApplication::exit(0);
        }
    });
    poll->start(100);
    QTimer::singleShot(15000, &widget, [] {
        std::fprintf(stderr, "spike: PROBE-TIMEOUT\n");
        QCoreApplication::exit(3);
    });
}

/**
 * @brief --local 链：ZzPty spawn bash 全链（任务 1 形态 + probe）。
 * @param probe 是否启用无人值守自检。
 * @return 进程退出码。
 */
int runLocal(bool probe)
{
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

    if (probe) {
        // bash 启动后从 PTY 输入缓冲读到该命令并回显标记。
        const std::string cmd = "echo zz-spike-probe-ok\n";
        pty->writeAll(std::as_bytes(std::span<const char>(cmd.data(), cmd.size())));
        armProbeWatch(widget);
    }

    // M14：滚动条与 widget 同窗装配（spike 最小形态：上限跟随历史行数）。
    QScrollBar scrollBar(Qt::Vertical);
    scrollBar.setRange(0, 0);
    QObject::connect(&widget, &ZzCoreViewWidget::scrollRangeChanged, &scrollBar,
                     &QScrollBar::setMaximum);
    QObject::connect(&widget, &ZzCoreViewWidget::scrollOffsetChanged, &scrollBar,
                     &QScrollBar::setValue);
    QObject::connect(&scrollBar, &QScrollBar::valueChanged, &widget,
                     &ZzCoreViewWidget::setScrollOffset);

    // window 堆分配：layout 会把栈对象 widget/scrollBar 收编为其子对象，
    // 栈展开时它们自重父摘除，window 由 QApplication 析构期统一回收，
    // 避免栈上 window 先析构删除子对象导致的 double free。
    auto* window = new QWidget;
    QHBoxLayout layout(window);
    layout.setContentsMargins(0, 0, 0, 0);
    layout.addWidget(&widget, 1);
    layout.addWidget(&scrollBar);
    window->resize(widget.sizeHint().width() + scrollBar.sizeHint().width(),
                   widget.sizeHint().height());
    window->show();
    return QCoreApplication::exec();
}

/**
 * @brief --ssh 链：ZzSshCore 直驱 shell channel 全链。
 * @param target 连接目标，形如 [user@]host[:port]（缺省 user=当前用户、port=22）。
 * @param keyPath 显式私钥路径；为空则按 OpenSSH 惯例探测默认私钥。
 * @param probe 是否启用无人值守自检（密码请求直接取消，仅 agent/公钥可达）。
 * @return 进程退出码。
 */
int runSsh(const QString& target, const QString& keyPath, bool probe)
{
    // 解析 [user@]host[:port]。
    QString user = qEnvironmentVariable("USER");
    QString rest = target;
    const int at = rest.indexOf(QLatin1Char('@'));
    if (at >= 0) {
        user = rest.left(at);
        rest = rest.mid(at + 1);
    }
    QString host = rest;
    quint16 port = 22;
    const int colon = rest.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0) {
        host = rest.left(colon);
        port = rest.mid(colon + 1).toUShort();
    }
    if (host.isEmpty() || user.isEmpty() || port == 0) {
        std::fprintf(stderr, "spike: --ssh 目标解析失败：%s（期望 [user@]host[:port]）\n",
                     qUtf8Printable(target));
        return 2;
    }

    ZzCoreViewWidget widget;
    widget.setWindowTitle(QStringLiteral("spike-ssh %1").arg(target));

    ZzSshConnection conn;
    conn.setConnectTimeout(5000);
    // spike 简化：TOFU 自动信任 + 临时目录 known_hosts（正式应用走确认弹窗与
    // AppConfigLocation，见 ZzSshTransport 注释）；指纹打印 stderr 供人工核对。
    conn.setKnownHostsFilePath(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                               + QStringLiteral("/zzcore_spike_known_hosts.json"));

    // 公钥认证：显式 --key 优先，否则探测默认私钥（ZzSshAuthConfig 仅在
    // privateKeyPath 非空时尝试公钥，不自动扫默认路径——与 OpenSSH 行为不同）。
    const QString key = keyPath.isEmpty() ? defaultPrivateKeyPath() : keyPath;
    if (!key.isEmpty()) {
        ZzSshAuthConfig auth;
        auth.privateKeyPath = key;
        std::fprintf(stderr, "spike: 公钥认证使用私钥 %s\n", qUtf8Printable(key));
        conn.setAuthConfig(auth);
    }

    ZzSshShellChannel* channel = nullptr; // connected 后创建；writeFn/resize 判空

    QObject::connect(&conn, &ZzSshConnection::errorOccurred, &widget,
                     [](int code, const QString& message) {
                         std::fprintf(stderr, "spike: SSH 错误(%d)：%s\n", code,
                                      qUtf8Printable(message));
                         QCoreApplication::exit(1);
                     });
    QObject::connect(&conn, &ZzSshConnection::disconnected, &widget,
                     [](const QString& reason) {
                         std::fprintf(stderr, "spike: SSH 断开：%s\n", qUtf8Printable(reason));
                         QCoreApplication::quit();
                     });
    // 认证链：agent -> 公钥 -> 密码兜底（ZzSshAuthConfig 默认顺序）；probe 无人值守
    // 直接取消密码请求（localhost 验证走当前用户公钥）。
    QObject::connect(&conn, &ZzSshConnection::passwordRequested, &widget, [&] {
        if (probe) {
            conn.cancelPasswordRequest();
            return;
        }
        bool ok = false;
        const QString password =
            QInputDialog::getText(&widget, QStringLiteral("spike-ssh"),
                                  QStringLiteral("%1@%2 密码：").arg(user, host),
                                  QLineEdit::Password, QString(), &ok);
        if (ok && !password.isEmpty())
            conn.providePassword(password);
        else
            conn.cancelPasswordRequest();
    });
    QObject::connect(&conn, &ZzSshConnection::hostKeyUnknown, &widget,
                     [&conn](const QString& host, quint16 port, const QString& keyType,
                             const QString& fingerprint) {
                         std::fprintf(stderr, "spike: 自动信任主机密钥 %s:%u（%s %s）\n",
                                      qUtf8Printable(host), port, qUtf8Printable(keyType),
                                      qUtf8Printable(fingerprint));
                         conn.trustHostKey();
                     });
    QObject::connect(&conn, &ZzSshConnection::hostKeyChanged, &widget,
                     [&conn](const QString& host, quint16 port, const QString&,
                             const QString&, const QString&) {
                         std::fprintf(stderr, "spike: 主机密钥与记录不符（%s:%u），拒绝连接。\n",
                                      qUtf8Printable(host), port);
                         conn.rejectHostKey();
                     });

    QObject::connect(&conn, &ZzSshConnection::connected, &widget, [&] {
        channel = conn.createShellChannel();
        if (!channel) {
            std::fprintf(stderr, "spike: createShellChannel 失败。\n");
            QCoreApplication::exit(1);
            return;
        }
        // 远端输出 -> feed -> 渲染。
        QObject::connect(channel, &ZzSshShellChannel::dataReceived, &widget,
                         [&widget](const QByteArray& data) {
                             widget.feedForTest(std::string_view(
                                 data.constData(), static_cast<std::size_t>(data.size())));
                         });
        QObject::connect(channel, &ZzSshShellChannel::shellOpened, &widget, [&] {
            if (probe)
                channel->write("echo zz-spike-probe-ok\n");
        });
        QObject::connect(channel, &ZzSshShellChannel::errorOccurred, &widget,
                         [](int code, const QString& message) {
                             std::fprintf(stderr, "spike: SSH 通道错误(%d)：%s\n", code,
                                          qUtf8Printable(message));
                             QCoreApplication::exit(1);
                         });
        QObject::connect(channel, &ZzSshShellChannel::closed, &widget,
                         [] { QCoreApplication::quit(); });

        const ZzSize grid = widget.terminal().size();
        channel->openShell(QStringLiteral("xterm-256color"), grid.cols, grid.rows);
    });

    // 键盘编码字节 -> SSH 通道。
    widget.setWriteFn([&channel](std::string bytes) {
        if (channel)
            channel->write(QByteArray::fromStdString(bytes));
    });

    // 网格尺寸变化 -> window-change（库侧尾随去抖 150ms 后转发远端）。
    QObject::connect(&widget, &ZzCoreViewWidget::gridResized, &widget,
                     [&channel](int cols, int rows) {
                         if (channel)
                             channel->resize(cols, rows);
                     });

    if (probe)
        armProbeWatch(widget);

    conn.connectToHost(host, port, user);
    // M14：滚动条与 widget 同窗装配（spike 最小形态：上限跟随历史行数）。
    QScrollBar scrollBar(Qt::Vertical);
    scrollBar.setRange(0, 0);
    QObject::connect(&widget, &ZzCoreViewWidget::scrollRangeChanged, &scrollBar,
                     &QScrollBar::setMaximum);
    QObject::connect(&widget, &ZzCoreViewWidget::scrollOffsetChanged, &scrollBar,
                     &QScrollBar::setValue);
    QObject::connect(&scrollBar, &QScrollBar::valueChanged, &widget,
                     &ZzCoreViewWidget::setScrollOffset);

    // window 堆分配：layout 会把栈对象 widget/scrollBar 收编为其子对象，
    // 栈展开时它们自重父摘除，window 由 QApplication 析构期统一回收，
    // 避免栈上 window 先析构删除子对象导致的 double free。
    auto* window = new QWidget;
    QHBoxLayout layout(window);
    layout.setContentsMargins(0, 0, 0, 0);
    layout.addWidget(&widget, 1);
    layout.addWidget(&scrollBar);
    window->resize(widget.sizeHint().width() + scrollBar.sizeHint().width(),
                   widget.sizeHint().height());
    window->show();
    return QCoreApplication::exec();
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    const QStringList args = app.arguments();
    const bool probe = args.contains(QStringLiteral("--probe"));
    const int sshIdx = args.indexOf(QStringLiteral("--ssh"));
    if (sshIdx >= 0) {
        if (sshIdx + 1 >= args.size()) {
            std::fprintf(stderr, "spike: --ssh 需要目标参数 [user@]host[:port]。\n");
            return 2;
        }
        QString keyPath;
        const int keyIdx = args.indexOf(QStringLiteral("--key"));
        if (keyIdx >= 0 && keyIdx + 1 < args.size())
            keyPath = args.at(keyIdx + 1);
        return runSsh(args.at(sshIdx + 1), keyPath, probe);
    }
    return runLocal(probe);
}
