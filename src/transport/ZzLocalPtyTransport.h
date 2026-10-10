#pragma once

#include <memory>

#include "ZzTransportInterface.h"

class ZzPty;
#if defined(Q_OS_WIN)
class ZzPtyReaderThread;
#else
class QSocketNotifier;
#endif

/**
 * @brief 本地 shell 传输：包装 ZzTerm::Pty（Unix openpty / Windows ConPTY）。
 *
 * 不走网络、不经 SSH，验证终端层与协议层解耦——对 ZzTerminalView 而言
 * 与 ZzSshTransport 完全同形。
 */
class ZzLocalPtyTransport : public ZzTransportInterface
{
    Q_OBJECT
public:
    explicit ZzLocalPtyTransport(QObject *parent = nullptr);
    ~ZzLocalPtyTransport() override;

    void open(const ZzTransportEndpoint &endpoint) override;
    void write(const QByteArray &data) override;
    void resize(int cols, int rows) override;
    void close() override;

private:
    /** @brief 子进程退出归一处理：排空余量后发射 disconnected（主动 close 抑制）。 */
    void handleShellExit();

    std::unique_ptr<ZzPty> m_pty;    ///< PTY 会话（RAII 持有）
    bool m_closing = false;          ///< 主动关闭标记：抑制重复 disconnected
#if defined(Q_OS_WIN)
    ZzPtyReaderThread *m_reader = nullptr; ///< ConPTY 阻塞读线程（HANDLE 无 QSocketNotifier）
#else
    QSocketNotifier *m_notifier = nullptr; ///< Unix master fd 可读通知
#endif
};
