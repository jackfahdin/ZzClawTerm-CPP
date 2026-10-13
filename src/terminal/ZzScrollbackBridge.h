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
 * 读回路径（Z2 任务 5 重建）：widget 越顶信号 historyTopReached 触发
 * injectOlderBlock——readOlderLines 委托引擎 getLines 取更早一块，经
 * M19 原语 prependHistoryLines 注入 Core 历史最旧端（纯文本、无样式）；
 * 变列 reflow（行身份重建）经 gridResized 做差分游标回锚，不重复追加，
 * reflow 同帧突发输出的新行不丢（任务 5 审查修复）。
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

    /**
     * @brief 从 LogEngine 读回一块更早历史注入 Core 最旧端（默认 200 行）。
     *
     * M19 减法口径：Core 最旧端的引擎绝对行号即 droppedLineCount() 基线
     * 本身（基线 = 累计裁掉 − 累计注入归还，随注入回退），无需额外对账
     * 成员。无更早历史或归还额度不足（Core 从未裁剪）时静默不动。
     */
    void injectOlderBlock(int maxLines = 200);

signals:
    /** @brief 引擎降级为纯内存模式（规格 §八：提示用户，不影响终端交互）。 */
    void degraded(const QString &reason);

private:
    /**
     * @brief 追加游标 m_seenLineNo 到 newest 之间的滚出定稿行（含 dropped
     *        跳变缺口跳齐）；contentFed/gridResized 共用。
     */
    void appendScrolloutsUpTo(std::uint64_t dropped, std::uint64_t count,
                              std::uint64_t newest);

    ZzCoreTerminalWidget *m_term;
    ZzLogEngine *m_engine;
    std::uint64_t m_seenLineNo = 0; ///< 追加对账游标：已落盘的绝对行号（含 dropped 偏移）
    std::uint64_t m_lastGeneration = 0; ///< 上一帧历史代计数（reflow/重组侦测）
    int m_lastCols = -1; ///< 上次 gridResized 的列数（列变 reflow 与纯行变甄别）
};
