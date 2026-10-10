#include "ZzLocalPtyTransport.h"

#include <QtCore/QSocketNotifier>
#include <QtCore/QThread>

#include <cstdio>
#include <span>
#include <string>

#include "ZzPty.h"

#if defined(Q_OS_WIN)
#include <QtCore/QCoreApplication>

#include <atomic>
#else
#include <fcntl.h>
#endif

namespace {

/** @brief 按平台取默认本地 shell（沿用 ptyqt 时代规则）。 */
QString zzDefaultShell()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("c:\\Windows\\system32\\WindowsPowerShell\\v1.0\\powershell.exe");
#else
    const QString fromEnv = QString::fromLocal8Bit(qgetenv("SHELL"));
    return fromEnv.isEmpty() ? QStringLiteral("/bin/sh") : fromEnv;
#endif
}

} // namespace

#if defined(Q_OS_WIN)
/**
 * @brief ConPTY 读取线程：HANDLE 管道无 QSocketNotifier 可用，
 * 阻塞 read 循环，读到数据/EOF 经 QueuedConnection 投递回 GUI 线程。
 */
class ZzPtyReaderThread : public QThread
{
    Q_OBJECT
public:
    ZzPty *pty = nullptr;   ///< 借用，生命周期由 transport 保证
    std::atomic<bool> stopping = false;

    void run() override
    {
        std::byte buf[64 * 1024];
        while (!stopping.load()) {
            const std::ptrdiff_t n = pty->read(std::span<std::byte>(buf, sizeof(buf)));
            if (n > 0) {
                const QByteArray data(reinterpret_cast<const char *>(buf), (qsizetype)n);
                QMetaObject::invokeMethod(qApp, [this, data]() {
                    emit dataReady(data);
                }, Qt::QueuedConnection);
                continue;
            }
            if (n == 0) { // EOF（子进程退出）
                QMetaObject::invokeMethod(qApp, [this]() { emit eofReached(); },
                                          Qt::QueuedConnection);
                return;
            }
            // -1：Windows 侧 ERROR_NO_DATA 等价 EAGAIN——阻塞读不会出现，
            // 防御性让出时间片
            QThread::msleep(1);
        }
    }

signals:
    void dataReady(const QByteArray &data);
    void eofReached();
};
#endif

ZzLocalPtyTransport::ZzLocalPtyTransport(QObject *parent)
    : ZzTransportInterface(parent)
{
}

ZzLocalPtyTransport::~ZzLocalPtyTransport()
{
    close();
}

void ZzLocalPtyTransport::open(const ZzTransportEndpoint &endpoint)
{
    if (state() != State::Disconnected) {
        return;
    }
    setState(State::Connecting);
    m_closing = false;

    const QString shell = endpoint.shellProgram.isEmpty()
        ? zzDefaultShell()
        : endpoint.shellProgram;
    ZzPtyConfig config;
    config.argv = {shell.toStdString()};
    config.cols = endpoint.cols;
    config.rows = endpoint.rows;
    config.rawMode = false; // 交互会话保持 slave echo/opost/isig
    m_pty = ZzPty::spawn(config);
    if (!m_pty) {
        setState(State::Disconnected);
        emit errorOccurred(2001, tr("启动本地 shell 失败（ZzPty spawn）"));
        return;
    }

#if defined(Q_OS_WIN)
    // ConPTY：HANDLE 管道走读取线程
    m_reader = new ZzPtyReaderThread();
    m_reader->pty = m_pty.get();
    connect(m_reader, &ZzPtyReaderThread::dataReady, this,
            [this](const QByteArray &data) { emit dataReceived(data); });
    connect(m_reader, &ZzPtyReaderThread::eofReached, this,
            [this]() { handleShellExit(); });
    m_reader->start();
#else
    // Unix：master fd 非阻塞 + QSocketNotifier 边界触发读循环
    fcntl(m_pty->masterFd(), F_SETFL, O_NONBLOCK);
    m_notifier = new QSocketNotifier(m_pty->masterFd(), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this]() {
        std::byte buf[64 * 1024];
        for (;;) {
            const std::ptrdiff_t n =
                m_pty->read(std::span<std::byte>(buf, sizeof(buf)));
            if (n > 0) {
                emit dataReceived(QByteArray(reinterpret_cast<const char *>(buf),
                                             (qsizetype)n));
                continue;
            }
            if (n == 0) { // EOF（子进程退出归一）
                handleShellExit();
            }
            break; // -1（含 EAGAIN）：本轮读完
        }
    });
#endif

    setState(State::Connected);
}

void ZzLocalPtyTransport::handleShellExit()
{
    if (!m_pty || state() == State::Disconnected) {
        return;
    }
    if (!m_closing) {
        setState(State::Disconnected);
        const auto code = m_pty->tryWait();
        emit disconnected(tr("本地 shell 已退出（退出码 %1）")
                              .arg(code ? *code : -1));
    }
}

void ZzLocalPtyTransport::write(const QByteArray &data)
{
    if (m_pty && state() == State::Connected) {
        m_pty->writeAll(std::as_bytes(std::span<const char>(data.constData(),
                                                            data.size())));
    }
}

void ZzLocalPtyTransport::resize(int cols, int rows)
{
    if (m_pty) {
        m_pty->resize(cols, rows);
    }
}

void ZzLocalPtyTransport::close()
{
    if (!m_pty) {
        setState(State::Disconnected);
        return;
    }
    m_closing = true;
#if defined(Q_OS_WIN)
    if (m_reader) {
        m_reader->stopping.store(true);
        m_pty.reset(); // ClosePseudoConsole 使阻塞读断裂退出
        m_reader->wait(2000);
        delete m_reader;
        m_reader = nullptr;
    } else {
        m_pty.reset();
    }
#else
    delete m_notifier;
    m_notifier = nullptr;
    m_pty.reset(); // 析构 SIGHUP 子进程并回收
#endif
    setState(State::Disconnected);
}

#if defined(Q_OS_WIN)
#include "ZzLocalPtyTransport.moc"
#endif
