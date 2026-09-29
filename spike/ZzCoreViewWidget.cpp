#include "ZzCoreViewWidget.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>

#include <span>

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
}

void ZzCoreViewWidget::setWriteFn(std::function<void(std::string)> fn)
{
    writeFn_ = std::move(fn);
}

void ZzCoreViewWidget::feedForTest(std::string_view data)
{
    term_.feed(std::as_bytes(std::span<const char>(data.data(), data.size())));
    update();
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
        const ZzLineView line = view.lineAt(row);
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
    if (cursor.visible && cursor.position.row >= 0 && cursor.position.row < grid.rows
        && cursor.position.col >= 0 && cursor.position.col < grid.cols) {
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
        emit gridResized(cols, rows);
        update();
    }
}
