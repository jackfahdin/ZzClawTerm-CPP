#include "ZzCoreViewWidget.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <span>

#include <cstdlib>
#include <cstring>

#include <ZzTerm/UnwrapView.h>

namespace {

/**
 * @brief Qt 修饰键映射为 ZzKeyModifier 位掩码。
 * @param mods Qt 键盘修饰键。
 * @return ZzTermCore 修饰键组合。
 */
[[nodiscard]] ZzKeyModifier mapModifiers(Qt::KeyboardModifiers mods) noexcept
{
    ZzKeyModifier out = ZzKeyModifier::None;
    if (mods & Qt::ShiftModifier)
        out = out | ZzKeyModifier::Shift;
    if (mods & Qt::AltModifier)
        out = out | ZzKeyModifier::Alt;
    if (mods & Qt::ControlModifier)
        out = out | ZzKeyModifier::Ctrl;
    if (mods & Qt::MetaModifier)
        out = out | ZzKeyModifier::Super;
    return out;
}

/**
 * @brief 逻辑坐标 (line,col) 是否落在选区半开区间 [start, end) 内（字典序）。
 * @param start 选区起点（逻辑行空间：折链合并，col 为内容流偏移）。
 * @param end 选区终点（不含）。
 * @param line 待测逻辑行号。
 * @param col 待测内容列。
 * @return 在选区内则为 true。
 */
[[nodiscard]] bool inSelectionRange(ZzLogicalPos start, ZzLogicalPos end,
                                    std::int64_t line, std::int64_t col) noexcept
{
    return (line > start.line || (line == start.line && col >= start.col))
           && (line < end.line || (line == end.line && col < end.col));
}

} // namespace

ZzCoreViewWidget::ZzCoreViewWidget(QWidget* parent)
    : QWidget(parent), term_(80, 24, ZzBackendKind::Native, 10000)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    updateCellMetrics();

    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    // 键盘编码字节与终端回传（DA1/DSR 等）统一经 output 通道发出，
    // 在此转发给 writeFn_；编码模式位由 ZzTerminal 与 feed 联动同步。
    term_.setOutputHandler([this](std::string_view bytes) {
        if (writeFn_)
            writeFn_(std::string(bytes));
    });

    // 操作留痕：ZZCORE_SPIKE_TRACE=<路径> 时把 feed 字节与网格 resize
    // 依序写入留痕文件，供离线重放定位 resize 相关问题（弃子诊断件）。
    if (const char* path = std::getenv("ZZCORE_SPIKE_TRACE"); path && *path) {
        trace_ = std::fopen(path, "wb");
        if (!trace_)
            std::fprintf(stderr, "spike: 留痕文件 %s 打开失败\n", path);
    }
}

void ZzCoreViewWidget::setWriteFn(std::function<void(std::string)> fn)
{
    writeFn_ = std::move(fn);
}

void ZzCoreViewWidget::feedForTest(std::string_view data)
{
    traceFeed(data);
    const int maxBefore  = maxScrollOffset();
    const int maxHBefore = maxHScrollOffset();
    term_.feed(std::as_bytes(std::span<const char>(data.data(), data.size())));
    if (maxScrollOffset() != maxBefore)
        emit scrollRangeChanged(maxScrollOffset());
    if (maxHScrollOffset() != maxHBefore) // M17b：拼接行最大长随 feed 变化
        emit hScrollRangeChanged(maxHScrollOffset());
    setScrollOffset(scrollOffset_); // 夹取（reflow/裁剪后偏移可能越界）
    followCursorHorizontally();
    update();
}

// 留痕格式（小端）：'F' u32len bytes / 'R' i32cols i32rows，逐条 flush。
void ZzCoreViewWidget::traceFeed(std::string_view data)
{
    if (!trace_)
        return;
    std::fputc('F', trace_);
    const std::uint32_t len = static_cast<std::uint32_t>(data.size());
    std::fwrite(&len, sizeof(len), 1, trace_);
    std::fwrite(data.data(), 1, data.size(), trace_);
    std::fflush(trace_);
}

void ZzCoreViewWidget::traceResize(int cols, int rows)
{
    if (!trace_)
        return;
    std::fputc('R', trace_);
    const std::int32_t c = cols, r = rows;
    std::fwrite(&c, sizeof(c), 1, trace_);
    std::fwrite(&r, sizeof(r), 1, trace_);
    std::fflush(trace_);
}

QSize ZzCoreViewWidget::sizeHint() const
{
    const ZzSize grid = term_.size();
    return {grid.cols * cellWidth_, grid.rows * cellHeight_};
}

void ZzCoreViewWidget::updateCellMetrics()
{
    const QFontMetrics fm(font());
    cellWidth_  = qMax(1, fm.horizontalAdvance(QLatin1Char('M')));
    cellHeight_ = qMax(1, fm.height());
    cellAscent_ = fm.ascent();
}

void ZzCoreViewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());

    if (unwrapMode_) {
        paintUnwrapped(painter);
    } else {
        paintWrapped(painter);
    }
}

void ZzCoreViewWidget::paintWrapped(QPainter& painter)
{
    const ZzRenderView& view = term_.renderView();
    const ZzSize      grid   = view.size();

    // spike 全量重绘：无增量、无配色（文本用默认前景色），
    // 空格与续格 text 为空串自然跳过。
    for (int row = 0; row < grid.rows; ++row) {
        const ZzLineView line = lineViewAtVisibleRow(row);
        const int        y    = row * cellHeight_ + cellAscent_;
        for (int col = 0; col < grid.cols; ++col) {
            const ZzCellView cell = line.cellAt(col);
            if (cell.text.empty())
                continue;
            painter.drawText(col * cellWidth_, y,
                             QString::fromStdString(cell.text));
        }
        // 选区反色高亮（如有）：本可见行与选区重叠段
        paintSelectionOnWrappedRow(painter, row);
    }

    // 光标：按 cursor() 位置画反色矩形并重绘该格文本。
    const ZzCursorState cursor = view.cursor();
    if (scrollOffset_ == 0 && cursor.visible && cursor.position.row >= 0
        && cursor.position.row < grid.rows && cursor.position.col >= 0
        && cursor.position.col < grid.cols) {
        const QRect cursorRect(cursor.position.col * cellWidth_,
                               cursor.position.row * cellHeight_,
                               cellWidth_, cellHeight_);
        painter.fillRect(cursorRect, palette().text());
        const ZzCellView under = view.lineAt(cursor.position.row).cellAt(cursor.position.col);
        if (!under.text.empty()) {
            painter.setPen(palette().base().color());
            painter.drawText(cursor.position.col * cellWidth_,
                             cursor.position.row * cellHeight_ + cellAscent_,
                             QString::fromStdString(under.text));
        }
    }
}

void ZzCoreViewWidget::paintUnwrapped(QPainter& painter)
{
    const ZzUnwrapView& view = term_.unwrapView();
    const ZzSize        grid = term_.size();
    // 拼接空间纵向视口：底部对齐，scrollOffset_ 为离底拼接行数。
    const std::size_t total = stitchedTotalLines();
    // 可见第 row 行的拼接索引 = total - scrollOffset_ - grid.rows + row
    //（视口高 grid.rows，钉底时末行 = total-1）。
    for (int row = 0; row < grid.rows; ++row) {
        const long long idx = (long long)total - scrollOffset_ - grid.rows + row;
        if (idx < 0 || idx >= (long long)total)
            continue; // 顶部留白（拼接行不足一屏）
        const ZzStitchedLineView line = view.lineAt((std::size_t)idx);
        const int y = row * cellHeight_ + cellAscent_;
        for (int c = hScrollOffset_;
             c < line.cellCount() && c - hScrollOffset_ < grid.cols; ++c) {
            const ZzCellView cell = line.cellAt(c);
            if (cell.text.empty())
                continue;
            painter.drawText((c - hScrollOffset_) * cellWidth_, y,
                             QString::fromStdString(cell.text));
        }
        // 选区反色高亮（如有）：本拼接行与选区重叠段
        paintSelectionOnStitchedRow(painter, (std::size_t)idx, row, line);
    }

    // 光标：物理坐标 → 拼接坐标 → 显示坐标（含横向跟随后的窗口）
    const ZzCursorState cursor = term_.cursor();
    if (scrollOffset_ == 0 && cursor.visible) {
        const auto hist = (std::int64_t)term_.historyView().lineCount();
        const ZzStitchedPos sp =
            view.toStitched(ZzLogicalPos{hist + cursor.position.row,
                                         cursor.position.col});
        // 显示行 = 拼接索引 - 视口顶索引
        const long long viewTop = (long long)total - scrollOffset_ - grid.rows;
        const long long row = (long long)sp.line - viewTop;
        const int col = (int)sp.col - hScrollOffset_;
        if (row >= 0 && row < grid.rows && col >= 0 && col < grid.cols) {
            const QRect cursorRect(col * cellWidth_, (int)row * cellHeight_,
                                   cellWidth_, cellHeight_);
            painter.fillRect(cursorRect, palette().text());
        }
    }
}

// 选区高亮最小反馈：反色矩形覆盖重叠段后，以高亮前景重绘该段文本。
// 选区坐标为逻辑行空间（折链合并）；逻辑行号 == 拼接行索引、内容流偏移与
// 拼接列同口径（Core 的 zzLogicalLineCount 与 ZzUnwrapView 同规则分链），
// 物理坐标经 toStitched 换算后与 [start, end) 逐格求交。
void ZzCoreViewWidget::paintSelectionOnWrappedRow(QPainter& painter, int row)
{
    ZzLogicalPos start, end;
    if (!term_.selectionRange(start, end))
        return;
    const auto hist = (std::int64_t)term_.historyView().lineCount();
    const std::int64_t u = hist - scrollOffset_ + row; // 本可见行的统一物理行号
    const ZzSize grid = term_.size();
    const ZzUnwrapView& view = term_.unwrapView();
    // 逐格换算逻辑坐标求交，得本行覆盖列段 [c0, c1]
    int c0 = -1, c1 = -1;
    for (int c = 0; c < grid.cols; ++c) {
        const ZzStitchedPos sp = view.toStitched(ZzLogicalPos{u, c});
        if (inSelectionRange(start, end, sp.line, sp.col)) {
            if (c0 < 0)
                c0 = c;
            c1 = c;
        }
    }
    if (c0 < 0)
        return;
    painter.fillRect(c0 * cellWidth_, row * cellHeight_,
                     (c1 - c0 + 1) * cellWidth_, cellHeight_, palette().highlight());
    painter.save(); // 高亮画笔不外泄——后续行的文本仍用默认前景色
    painter.setPen(palette().highlightedText().color());
    const ZzLineView line = lineViewAtVisibleRow(row);
    const int y = row * cellHeight_ + cellAscent_;
    for (int c = c0; c <= c1 && c < line.cellCount(); ++c) {
        const ZzCellView cell = line.cellAt(c);
        if (!cell.text.empty())
            painter.drawText(c * cellWidth_, y, QString::fromStdString(cell.text));
    }
    painter.restore();
}

void ZzCoreViewWidget::paintSelectionOnStitchedRow(QPainter& painter, std::size_t idx,
                                                   int row, const ZzStitchedLineView& line)
{
    ZzLogicalPos start, end;
    if (!term_.selectionRange(start, end))
        return;
    // 拼接坐标即逻辑坐标：直接按 (idx, c) 与 [start, end) 求交。
    const ZzSize grid = term_.size();
    const int n = line.cellCount();
    int c0 = -1, c1 = -1; // 被覆盖的拼接列段 [c0, c1]
    for (int c = 0; c < n; ++c) {
        if (inSelectionRange(start, end, (std::int64_t)idx, c)) {
            if (c0 < 0)
                c0 = c;
            c1 = c;
        }
    }
    // 行尾亦在选区内（跨行选区的中间行/末行内容末端）时补满显示窗口余宽
    if (inSelectionRange(start, end, (std::int64_t)idx, n)) {
        if (c0 < 0)
            c0 = 0;
        c1 = hScrollOffset_ + grid.cols - 1;
    }
    if (c0 < 0)
        return;
    // 扣除横向偏移，夹取到可见窗口
    const int w0 = qMax(c0 - hScrollOffset_, 0);
    const int w1 = qMin(c1 - hScrollOffset_, grid.cols - 1);
    if (w0 > w1)
        return;
    painter.fillRect(w0 * cellWidth_, row * cellHeight_,
                     (w1 - w0 + 1) * cellWidth_, cellHeight_, palette().highlight());
    painter.save(); // 高亮画笔不外泄——后续行的文本仍用默认前景色
    painter.setPen(palette().highlightedText().color());
    const int y = row * cellHeight_ + cellAscent_;
    for (int c = w0 + hScrollOffset_;
         c <= qMin(c1, n - 1) && c - hScrollOffset_ < grid.cols; ++c) {
        const ZzCellView cell = line.cellAt(c);
        if (!cell.text.empty())
            painter.drawText((c - hScrollOffset_) * cellWidth_, y,
                             QString::fromStdString(cell.text));
    }
    painter.restore();
}

void ZzCoreViewWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)
        && event->key() == Qt::Key_U) {
        setUnwrapMode(!unwrapMode_); // M17b：Ctrl+Shift+U 切换不换行显示
        event->accept();
        return;
    }
    if (scrollOffset_ != 0)
        setScrollOffset(0);
    ZzKeyEvent keyEvent;
    keyEvent.action    = ZzKeyEvent::Action::Press;
    keyEvent.modifiers = mapModifiers(event->modifiers());

    using Key = ZzKeyEvent::Key;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:    keyEvent.key = Key::Enter;     break;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:  keyEvent.key = Key::Tab;       break;
    case Qt::Key_Backspace: keyEvent.key = Key::Backspace; break;
    case Qt::Key_Escape:   keyEvent.key = Key::Escape;    break;
    case Qt::Key_Up:       keyEvent.key = Key::Up;        break;
    case Qt::Key_Down:     keyEvent.key = Key::Down;      break;
    case Qt::Key_Left:     keyEvent.key = Key::Left;      break;
    case Qt::Key_Right:    keyEvent.key = Key::Right;     break;
    case Qt::Key_Home:     keyEvent.key = Key::Home;      break;
    case Qt::Key_End:      keyEvent.key = Key::End;       break;
    case Qt::Key_Insert:   keyEvent.key = Key::Insert;    break;
    case Qt::Key_Delete:   keyEvent.key = Key::Delete;    break;
    case Qt::Key_PageUp:   keyEvent.key = Key::PageUp;    break;
    case Qt::Key_PageDown: keyEvent.key = Key::PageDown;  break;
    case Qt::Key_F1:       keyEvent.key = Key::F1;        break;
    case Qt::Key_F2:       keyEvent.key = Key::F2;        break;
    case Qt::Key_F3:       keyEvent.key = Key::F3;        break;
    case Qt::Key_F4:       keyEvent.key = Key::F4;        break;
    case Qt::Key_F5:       keyEvent.key = Key::F5;        break;
    case Qt::Key_F6:       keyEvent.key = Key::F6;        break;
    case Qt::Key_F7:       keyEvent.key = Key::F7;        break;
    case Qt::Key_F8:       keyEvent.key = Key::F8;        break;
    case Qt::Key_F9:       keyEvent.key = Key::F9;        break;
    case Qt::Key_F10:      keyEvent.key = Key::F10;       break;
    case Qt::Key_F11:      keyEvent.key = Key::F11;       break;
    case Qt::Key_F12:      keyEvent.key = Key::F12;       break;
    default:               keyEvent.key = Key::Character; break;
    }

    // 功能键（含修饰组合）走 encodeKey 语义；普通字符在下方按文本处理。
    if (keyEvent.key != Key::Character) {
        term_.sendKey(keyEvent);
        event->accept();
        return;
    }

    const bool ctrlOrAlt = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier);
    if (!ctrlOrAlt) {
        // 可打印文本（含 CJK 等 Unicode 输入）走 encodeText 语义。
        const QString text = event->text();
        if (!text.isEmpty()) {
            term_.sendText(text.toUtf8().toStdString());
            event->accept();
            return;
        }
    } else if (event->key() >= 0x20 && event->key() < 0x7F) {
        // Ctrl/Alt + ASCII：交由 Core 编码（Ctrl+字母 -> C0 控制字节等）。
        keyEvent.character = static_cast<char32_t>(event->key());
        term_.sendKey(keyEvent);
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void ZzCoreViewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    const int cols = qMax(1, event->size().width() / cellWidth_);
    const int rows = qMax(1, event->size().height() / cellHeight_);
    const ZzSize current = term_.size();
    if (cols == current.cols && rows == current.rows)
        return;

    const int maxHBefore = maxHScrollOffset(); // M17b：resize 前横向口径快照
    if (term_.resize(cols, rows)) {
        traceResize(cols, rows);
        emit gridResized(cols, rows);
        emit scrollRangeChanged(maxScrollOffset()); // M14：reflow 改变历史行数
        if (maxHScrollOffset() != maxHBefore)       // M17b：列数变化改变横向上限
            emit hScrollRangeChanged(maxHScrollOffset());
        setScrollOffset(scrollOffset_);             // 夹取
        followCursorHorizontally();
        update();
    }
}

int ZzCoreViewWidget::maxScrollOffset() const noexcept
{
    if (unwrapMode_)
        return stitchedHistoryCount(); // 拼接空间：完全落在历史区的拼接行数
    return static_cast<int>(term_.historyView().lineCount()); // Alternate 屏契约恒 0
}

int ZzCoreViewWidget::stitchedHistoryCount() const
{
    const ZzUnwrapView& view = term_.unwrapView();
    const auto hist = (std::int64_t)term_.historyView().lineCount();
    int count = 0;
    const std::size_t n = view.lineCount();
    for (std::size_t i = 0; i < n; ++i) {
        const ZzStitchedLineView line = view.lineAt(i);
        if (line.sourceLine() + line.sourceLineCount() <= hist)
            ++count; // 完全在历史区；跨缝链归屏幕侧
    }
    return count;
}

std::size_t ZzCoreViewWidget::stitchedTotalLines() const
{
    return term_.unwrapView().lineCount();
}

void ZzCoreViewWidget::setUnwrapMode(bool on)
{
    if (unwrapMode_ == on)
        return;
    unwrapMode_ = on;
    if (!on)
        setHScrollOffset(0); // 回物理模式横向归零
    setScrollOffset(scrollOffset_); // 夹取：纵向口径随模式切换可能收缩
    emit hScrollRangeChanged(maxHScrollOffset());
    emit scrollRangeChanged(maxScrollOffset()); // 纵向口径随模式切换
    update();
}

int ZzCoreViewWidget::maxHScrollOffset() const noexcept
{
    if (!unwrapMode_)
        return 0;
    return qMax(0, term_.unwrapView().maxCellCount() - term_.size().cols);
}

void ZzCoreViewWidget::setHScrollOffset(int offset)
{
    offset = qBound(0, offset, maxHScrollOffset());
    if (offset == hScrollOffset_)
        return;
    hScrollOffset_ = offset;
    emit hScrollOffsetChanged(hScrollOffset_);
    update();
}

void ZzCoreViewWidget::followCursorHorizontally()
{
    if (!unwrapMode_)
        return;
    const ZzCursorState cursor = term_.cursor();
    if (!cursor.visible)
        return;
    const auto hist = (std::int64_t)term_.historyView().lineCount();
    const ZzStitchedPos sp = term_.unwrapView().toStitched(
        ZzLogicalPos{hist + cursor.position.row, cursor.position.col});
    const int cols = term_.size().cols;
    if ((int)sp.col < hScrollOffset_)
        setHScrollOffset((int)sp.col);
    else if ((int)sp.col >= hScrollOffset_ + cols)
        setHScrollOffset((int)sp.col - cols + 1); // 贴右边
}

void ZzCoreViewWidget::setScrollOffset(int offset)
{
    offset = qBound(0, offset, maxScrollOffset());
    if (offset == scrollOffset_)
        return;
    scrollOffset_ = offset;
    emit scrollOffsetChanged(scrollOffset_);
    update();
}

ZzLineView ZzCoreViewWidget::lineViewAtVisibleRow(int row) const
{
    // 统一坐标：历史区 [0, hist) 在前、屏幕区 [hist, hist+rows) 在后；
    // 视口底部对齐，scrollOffset_ 为离底行数。
    const auto hist = term_.historyView().lineCount();
    const auto u    = hist - static_cast<std::size_t>(scrollOffset_)
                    + static_cast<std::size_t>(row);
    if (u < hist)
        return term_.historyView().lineAt(u);
    return term_.renderView().lineAt(static_cast<int>(u - hist));
}

QString ZzCoreViewWidget::visibleLineTextForTest(int row) const
{
    const ZzLineView line = lineViewAtVisibleRow(row);
    QString out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += QString::fromStdString(line.cellAt(col).text);
    return out;
}

void ZzCoreViewWidget::wheelEvent(QWheelEvent* event)
{
    if (term_.isAlternateScreen()) { // Alternate 无历史（契约 lineCount 恒 0）
        event->ignore();
        return;
    }
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0) {
        scrollByWheelStepsForTest(steps);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void ZzCoreViewWidget::scrollByWheelStepsForTest(int steps)
{
    // offset 语义为离底行数：上滚（steps > 0）增大 offset 进入历史，每格 3 行。
    setScrollOffset(scrollOffset_ + steps * 3);
}

QString ZzCoreViewWidget::visibleStitchedTextForTest(int row) const
{
    const ZzUnwrapView& view = term_.unwrapView();
    const ZzSize        grid = term_.size();
    const std::size_t   total = view.lineCount();
    const long long idx = (long long)total - scrollOffset_ - grid.rows + row;
    if (idx < 0 || idx >= (long long)total)
        return QString();
    const ZzStitchedLineView line = view.lineAt((std::size_t)idx);
    QString out;
    for (int c = hScrollOffset_;
         c < line.cellCount() && c - hScrollOffset_ < grid.cols; ++c)
        out += QString::fromStdString(line.cellAt(c).text);
    return out;
}

int ZzCoreViewWidget::visibleStitchedCountForTest() const
{
    const ZzSize grid = term_.size();
    const long long total = (long long)term_.unwrapView().lineCount();
    return (int)qMax<long long>(0, qMin<long long>(grid.rows, total - scrollOffset_));
}

ZzLogicalPos ZzCoreViewWidget::logicalPosAtPixel(QPoint pos) const
{
    const int row = qBound(0, pos.y() / cellHeight_, term_.size().rows - 1);
    const int col = qBound(0, pos.x() / cellWidth_, term_.size().cols - 1);
    // 选区/搜索的 ZzLogicalPos 是逻辑行空间（折链合并为一条，col 为内容流
    // 偏移）；ZzUnwrapView 拼接坐标与其一一对应（拼接行索引 == 逻辑行号、
    // 拼接列 == 内容流偏移），物理统一坐标经 toStitched 换算。注意：
    // fromStitched 输出的是物理统一坐标，不能直接喂 setSelection。
    if (unwrapMode_) {
        const ZzUnwrapView& view = term_.unwrapView();
        const std::size_t total = view.lineCount();
        const long long idx =
            (long long)total - scrollOffset_ - term_.size().rows + row;
        if (idx < 0)
            return {0, 0};
        return {(std::int64_t)idx, col + hScrollOffset_}; // 拼接坐标即逻辑坐标
    }
    // 物理模式：显示坐标 → 统一物理坐标 → toStitched 换算逻辑坐标
    const auto hist = (std::int64_t)term_.historyView().lineCount();
    const ZzStitchedPos sp =
        term_.unwrapView().toStitched(ZzLogicalPos{hist - scrollOffset_ + row, col});
    return {sp.line, sp.col};
}

void ZzCoreViewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        selecting_ = true;
        selAnchor_ = logicalPosAtPixel(event->pos());
        term_.setSelection(selAnchor_, selAnchor_);
        update();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ZzCoreViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (selecting_) {
        // 不换行模式：拖拽越过左右缘时横向视口步进（选中屏外内容的通道）。
        if (unwrapMode_) {
            if (event->pos().x() >= width())
                setHScrollOffset(hScrollOffset_ + 1);
            else if (event->pos().x() < 0)
                setHScrollOffset(hScrollOffset_ - 1);
        }
        // 引擎 extendSelection 对空选区（anchor==extent）会重置锚点
        //（Terminal.cpp 注释明示的设计语义），故锚点自行保存、
        // 每次移动以 setSelection(锚点, 当前) 重放全量选区。
        // 端点粒度：悬停格计入选区（半开区间 [start, end)）——向前选
        // extent = 格索引+1，向后选锚点侧 +1，保证行末/边缘格可选中。
        const ZzLogicalPos cur = logicalPosAtPixel(event->pos());
        const bool forward = cur.line > selAnchor_.line
                             || (cur.line == selAnchor_.line && cur.col >= selAnchor_.col);
        if (forward)
            term_.setSelection(selAnchor_, {cur.line, cur.col + 1});
        else
            term_.setSelection({selAnchor_.line, selAnchor_.col + 1}, cur);
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ZzCoreViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (selecting_ && event->button() == Qt::LeftButton) {
        selecting_ = false;
        if (term_.hasSelection()) {
            const std::string text = term_.selectedText();
            if (!text.empty())
                QGuiApplication::clipboard()->setText(
                    QString::fromStdString(text)); // 复制完整拼接行（含屏外）
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}
