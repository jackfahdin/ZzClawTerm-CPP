#pragma once

#include <QtCore/QObject>
#include <QtCore/QStringList>

#include <cstdint>

class ZzCoreTerminalWidget;
class ZzLogEngine;

/**
 * @brief 滚动历史桥（规格 §5.4）：ZzCoreTerminalWidget 滚出（scrollout）的
 *        定稿行进 ZzLogEngine。
 *
 * 追加路径（Z1 落盘换源）：contentFed 后对 HistoryView 做绝对行号差分——
 * 新增的 scrollout 定稿行取纯文本 appendLine（比旧 dupDisplayOutput 显示
 * 镜像干净，无重绘噪音；wrapped 折链按物理行落盘，与旧逐显示行口径一致）。
 * 读回路径：readOlderLines 委托引擎 getLines（夹取到当前可读窗口）保留，
 * 供 Z2 读回桥复用；显示层注入（旧 setHistoryProvider 接线）已随
 * QTermWidget 摘除，Z2 特性 3 重建。
 * 降级路径：引擎 degradedToMemoryOnly → degraded 信号 → 状态栏提示（规格 §八）。
 */
class ZzScrollbackBridge : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief 绑定终端与日志引擎（均不拥有，调用方保证存活期覆盖本桥）。
     */
    ZzScrollbackBridge(ZzCoreTerminalWidget *term, ZzLogEngine *engine,
                       QObject *parent = nullptr);

    /**
     * @brief 读回绝对行号 [beforeLine - maxLines, beforeLine) 内的行文本
     *        （旧→新顺序，夹取到引擎当前可读窗口；供滚动加载与测试）。
     */
    [[nodiscard]] QStringList readOlderLines(qint64 beforeLine, int maxLines) const;

    /** @brief 引擎当前总行数。 */
    [[nodiscard]] qint64 totalLines() const;

    /** @brief 测试辅助：直接转发一次降级事件（等价引擎 I/O 失败）。 */
    void simulateDegradationForTest(const QString &reason);

signals:
    /** @brief 引擎降级为纯内存模式（规格 §八：提示用户，不影响终端交互）。 */
    void degraded(const QString &reason);

private:
    ZzCoreTerminalWidget *m_term;
    ZzLogEngine *m_engine;
    std::uint64_t m_seenLineNo = 0; ///< 追加对账游标：已落盘的绝对行号（含 dropped 偏移）
};
