#include "ZzScrollbackBridge.h"

#include "log/ZzLogEngine.h"
#include "log/ZzLogLine.h"
#include "terminal/ZzCoreTerminalWidget.h"

ZzScrollbackBridge::ZzScrollbackBridge(ZzCoreTerminalWidget *term,
                                       ZzLogEngine *engine, QObject *parent)
    : QObject(parent)
    , m_term(term)
    , m_engine(engine)
{
    // 追加路径（M17 口径）：feed 后 HistoryView 代计数差分——新增的
    // scrollout 定稿行取纯文本追加。比 dupDisplayOutput 显示镜像干净
    //（无重绘噪音）；读回注入留 Z2。
    connect(m_term, &ZzCoreTerminalWidget::contentFed, this, [this]() {
        const auto &hv = m_term->terminal().historyView();
        const std::size_t count = hv.lineCount();
        const std::uint64_t dropped = hv.droppedLineCount();
        // 绝对序号对账：seen 为已追加的绝对行号（含 dropped 偏移）
        std::uint64_t newest = dropped + count;
        while (m_seenLineNo < newest) {
            const std::size_t idx = (std::size_t)(m_seenLineNo - dropped);
            if (idx >= count) // dropped 跳变超过未同步段：跳过缺口
                break;
            const ZzLineView line = hv.lineAt(idx);
            QString text;
            const int n = line.cellCount();
            for (int c = 0; c < n; ++c)
                text += QString::fromStdString(line.cellAt(c).text);
            while (text.endsWith(QLatin1Char(' ')))
                text.chop(1);
            m_engine->appendLine({text, QByteArray()});
            ++m_seenLineNo;
        }
        if (m_seenLineNo < dropped) // 缺口跳齐
            m_seenLineNo = dropped;
    });
    // 降级路径（规格 §八）：引擎 I/O 失败 → 状态栏提示，不打断终端
    connect(m_engine, &ZzLogEngine::degradedToMemoryOnly, this,
            [this](const QString &reason) { emit degraded(reason); });
}

QStringList ZzScrollbackBridge::readOlderLines(qint64 beforeLine,
                                               int maxLines) const
{
    if (maxLines <= 0 || beforeLine <= 0) {
        return {};
    }
    const qint64 first = qint64(m_engine->firstLineNo());
    qint64 start = beforeLine - maxLines;
    if (start < first) {
        start = first;
    }
    if (start < 0 || start >= beforeLine) {
        return {};
    }
    const QVector<ZzLogLine> lines =
        m_engine->getLines(quint64(start), quint64(beforeLine - start));
    QStringList out;
    out.reserve(lines.size());
    for (const ZzLogLine &line : lines) {
        out.append(line.text);
    }
    return out;
}

qint64 ZzScrollbackBridge::totalLines() const
{
    return qint64(m_engine->totalLines());
}

void ZzScrollbackBridge::simulateDegradationForTest(const QString &reason)
{
    emit degraded(reason);
}
