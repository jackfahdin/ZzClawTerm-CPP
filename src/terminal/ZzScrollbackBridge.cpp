#include "ZzScrollbackBridge.h"

#include <ZzTerm/Line.h>
#include <ZzTerm/UnicodeWidth.h>

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
    //（无重绘噪音）；读回注入经 historyTopReached → injectOlderBlock。
    connect(m_term, &ZzCoreTerminalWidget::contentFed, this, [this]() {
        const auto &hv = m_term->terminal().historyView();
        const std::size_t count = hv.lineCount();
        const std::uint64_t dropped = hv.droppedLineCount();
        // 绝对序号对账：seen 为已追加的绝对行号（含 dropped 偏移）
        std::uint64_t newest = dropped + count;
        const std::uint64_t gen = hv.generation();
        if (gen != m_lastGeneration && newest < m_seenLineNo)
            // reflow/重组使行身份重建（dropped 不计重组）：游标回锚，
            // 已落盘内容按旧形态保留，不重复追加（规格 §4.4 对账复位）
            m_seenLineNo = newest;
        m_lastGeneration = gen;
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
    // 读回路径（Z2）：滚轮/滚动条越顶 → 注入一块更早历史到 Core 最旧端
    connect(m_term, &ZzCoreTerminalWidget::historyTopReached, this,
            [this]() { injectOlderBlock(); });
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

void ZzScrollbackBridge::injectOlderBlock(int maxLines)
{
    const auto &hv = m_term->terminal().historyView();
    // Core 历史最旧行的引擎绝对行号（M19 减法口径：基线 = 累计裁掉 −
    // 累计注入归还，注入使基线回退，故基线本身即 Core 最旧端的引擎行号）
    const qint64 oldestInCore = qint64(hv.droppedLineCount());
    const QStringList older = readOlderLines(oldestInCore, maxLines);
    if (older.isEmpty())
        return; // 无更早历史：静默停在越顶处（规格 §5）

    const int cols = m_term->terminal().size().cols;
    std::vector<ZzLine> lines;
    lines.reserve(std::size_t(older.size()));
    for (const QString &text : older) {
        ZzLine line(cols);
        int col = 0;
        for (const char32_t cp : text.toUcs4()) { // UCS-4 迭代：BMP 外按整码位
            if (col >= cols)
                break; // 读回行超宽截断（纯文本口径，丢样式为既有口径）
            ZzCell cell;
            cell.setCodePoint(cp);
            // 宽度取引擎同一张 EAW 表（zzCellWidthOf），与渲染口径一致
            const bool wide = zzCellWidthOf(cp) == 2;
            cell.setWidth(wide ? ZzCellWidth::WideLead : ZzCellWidth::Narrow);
            line.setCell(col++, cell);
            if (wide && col < cols) { // 宽字符占两格，续格占位
                ZzCell cont;
                cont.setWidth(ZzCellWidth::WideContinuation);
                line.setCell(col++, cont);
            }
        }
        lines.push_back(std::move(line));
    }
    // M19 原语（ZzTerminal::prependHistoryLines）：注入历史最旧端，
    // droppedLineCount 基线同步回退 N，既有行绝对行号（基线+idx）与
    // 追加差分 newest（基线+lineCount）跨注入恒等，差分天然跳过注入行
    if (m_term->terminal().prependHistoryLines(std::move(lines)))
        m_term->notifyHistoryPrepended();
}

void ZzScrollbackBridge::simulateDegradationForTest(const QString &reason)
{
    emit degraded(reason);
}
