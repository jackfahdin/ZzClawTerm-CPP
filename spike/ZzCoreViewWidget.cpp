#include "ZzCoreViewWidget.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <span>

#include <cstdlib>
#include <cstring>

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
    const int maxBefore = maxScrollOffset();
    term_.feed(std::as_bytes(std::span<const char>(data.data(), data.size())));
    if (maxScrollOffset() != maxBefore)
        emit scrollRangeChanged(maxScrollOffset());
    setScrollOffset(scrollOffset_); // 夹取（reflow/裁剪后偏移可能越界）
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

void ZzCoreViewWidget::keyPressEvent(QKeyEvent* event)
{
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

    if (term_.resize(cols, rows)) {
        traceResize(cols, rows);
        emit gridResized(cols, rows);
        emit scrollRangeChanged(maxScrollOffset()); // M14：reflow 改变历史行数
        setScrollOffset(scrollOffset_);             // 夹取
        update();
    }
}

int ZzCoreViewWidget::maxScrollOffset() const noexcept
{
    return static_cast<int>(term_.historyView().lineCount()); // Alternate 屏契约恒 0
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
