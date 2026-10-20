#include "ZzCoreTerminalWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QTimer>

#include <ZzFluentUI/ZzToolTip.h>
#include <QUrl>
#include <QWheelEvent>

#include <span>

#include <cstdlib>
#include <cstring>

#include <ZzTerm/UnwrapView.h>

#include "settings/ZzAppSettings.h"

#include "ZzTerminalPalette.h"

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

ZzBackendKind backendFromEnv() noexcept
{
#ifdef ZZCLAWTERM_WITH_CONTOUR
    // 实验演示开关：ZZ_TERM_BACKEND=contour 切到 contour 后端；
    // 默认 Native。contour 未编译进引擎时此开关不存在（编译期剔除）
    if (qgetenv("ZZ_TERM_BACKEND") == "contour")
        return ZzBackendKind::Contour;
#endif
    return ZzBackendKind::Native;
}

} // namespace

ZzCoreTerminalWidget::ZzCoreTerminalWidget(QWidget *parent)
    : QWidget(parent), term_(80, 24, backendFromEnv(), 10000)
{
    scheme_     = ZzTerminalPalette::scheme(ZzTerminalPalette::defaultSchemeName());
    schemeName_ = ZzTerminalPalette::defaultSchemeName();

    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    updateCellMetrics();

    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setMouseTracking(true); // M22b：无按键移动也要 hover 检测（Ctrl+悬停链接）

    // 键盘编码字节与终端回传（DA1/DSR 等）统一经 output 通道发出，
    // 在此转发为 sendData 信号；编码模式位由 ZzTerminal 与 feed 联动同步。
    term_.setOutputHandler([this](std::string_view bytes) {
        emit sendData(QByteArray(bytes.data(), (qsizetype)bytes.size()));
    });

    // 操作留痕：ZZCORE_SPIKE_TRACE=<路径> 时把 feed 字节与网格 resize
    // 依序写入留痕文件，供离线重放定位 resize 相关问题（弃子诊断件）。
    if (const char *path = std::getenv("ZZCORE_SPIKE_TRACE"); path && *path) {
        trace_ = std::fopen(path, "wb");
        if (!trace_)
            std::fprintf(stderr, "spike: 留痕文件 %s 打开失败\n", path);
    }
}

void ZzCoreTerminalWidget::recvData(const QByteArray &data)
{
    traceFeed(std::string_view(data.constData(), static_cast<std::size_t>(data.size())));
    const int maxBefore  = maxScrollOffset();
    const int maxHBefore = maxHScrollOffset();
    term_.feed(std::as_bytes(
        std::span<const char>(data.constData(), static_cast<std::size_t>(data.size()))));
    emit contentFed();
    if (maxScrollOffset() != maxBefore)
        emit scrollRangeChanged(maxScrollOffset());
    if (maxHScrollOffset() != maxHBefore) // M17b：拼接行最大长随 feed 变化
        emit hScrollRangeChanged(maxHScrollOffset());
    setScrollOffset(scrollOffset_); // 夹取（reflow/裁剪后偏移可能越界）
    followCursorHorizontally();
    update();
}

// 留痕格式（小端）：'F' u32len bytes / 'R' i32cols i32rows，逐条 flush。
void ZzCoreTerminalWidget::traceFeed(std::string_view data)
{
    if (!trace_)
        return;
    std::fputc('F', trace_);
    const std::uint32_t len = static_cast<std::uint32_t>(data.size());
    std::fwrite(&len, sizeof(len), 1, trace_);
    std::fwrite(data.data(), 1, data.size(), trace_);
    std::fflush(trace_);
}

void ZzCoreTerminalWidget::traceResize(int cols, int rows)
{
    if (!trace_)
        return;
    std::fputc('R', trace_);
    const std::int32_t c = cols, r = rows;
    std::fwrite(&c, sizeof(c), 1, trace_);
    std::fwrite(&r, sizeof(r), 1, trace_);
    std::fflush(trace_);
}

void ZzCoreTerminalWidget::updateCellMetrics()
{
    const QFontMetrics fm(font());
    cellWidth_  = qMax(1, fm.horizontalAdvance(QLatin1Char('M')));
    cellHeight_ = qMax(1, fm.height());
    cellAscent_ = fm.ascent();
}

QSize ZzCoreTerminalWidget::minimumSizeHint() const
{
    return {24 * cellWidth_, 6 * cellHeight_};
}

void ZzCoreTerminalWidget::recomputeGridForCurrentSize()
{
    // 最小网格钳制（fork setMinimumTerminalSize(24,6) 等价物）：控件被布局
    // 压得更小时网格保持 24×6，多余内容裁剪显示——防护语义优先于像素贴合。
    const int    cols    = qMax(24, size().width() / cellWidth_);
    const int    rows    = qMax(6,  size().height() / cellHeight_);
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

void ZzCoreTerminalWidget::setTerminalFontPointSize(int pointSize)
{
    QFont f = font();
    f.setPointSize(pointSize);
    setFont(f);
    updateCellMetrics();

    // 字号变更改变 cell 像素尺寸，按当前控件尺寸重算网格并沿用 resizeEvent
    // 的既有链路口径（gridResized → 宿主去抖转发 transport->resize）；
    // resizeEvent 是控件尺寸驱动，这里是字体驱动，换算与发射逻辑一致。
    // 隐藏期不重算：从未 show 的控件 size() 是 Qt 默认占位尺寸（100x30），
    // show 过再隐藏的虽保留最后真实尺寸，但隐藏期尺寸可能再变且
    // resizeEvent 只在可见后投递——统一推迟到 show 后的
    // resizeEvent/showEvent 按新 cell 尺寸一次算准（幂等）。
    if (isVisible())
        recomputeGridForCurrentSize();

    updateGeometry();
    update();
}

QColor ZzCoreTerminalWidget::resolveCellColor(ZzColor color, bool isForeground) const
{
    switch (color.kind()) {
    case ZzColor::Kind::Rgb:
        return QColor(color.red(), color.green(), color.blue());
    case ZzColor::Kind::Indexed:
        return ZzTerminalPalette::colorForIndex(scheme_, color.index());
    default: // Default：对齐当前方案默认前景/背景（修 Z1 0xcc 与方案不一致）
        return isForeground ? scheme_.foreground : scheme_.background;
    }
}

void ZzCoreTerminalWidget::setColorScheme(const QString &name)
{
    const QString effective = ZzTerminalPalette::availableSchemes().contains(name)
                                  ? name
                                  : ZzTerminalPalette::defaultSchemeName();
    if (schemeName_ == effective)
        return;
    schemeName_ = effective;
    scheme_     = ZzTerminalPalette::scheme(effective);
    update();
}

void ZzCoreTerminalWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), resolveCellColor(ZzColor::Default(), false));

    if (unwrapMode_) {
        paintUnwrapped(painter);
    } else {
        paintWrapped(painter);
    }
}

void ZzCoreTerminalWidget::paintWrapped(QPainter &painter)
{
    const ZzRenderView &view = term_.renderView();
    const ZzSize      grid   = view.size();
    // M22b：Ctrl 按住期间所有链接格画下划线；hover 命中的同 id 段画半透明高亮
    const bool linksActive = ZzAppSettings::instance().hyperlinkEnabled();
    QColor linkHighlight;
    if (linksActive && hoverHyperlinkId_ != 0) {
        linkHighlight = palette().highlight().color();
        linkHighlight.setAlpha(60);
    }

    // 全量重绘：无增量。每行两遍制——先铺整行背景、后画整行文本：
    // 双宽字符（WideLead 首格带文本 + WideContinuation 续格空串）的字形
    // 横跨两格，单遍「铺一格背景画一格文本」会让续格的背景填充盖掉
    // 左格字形的右半（选区路径先铺后画，故一直正常）。
    for (int row = 0; row < grid.rows; ++row) {
        const ZzLineView line = lineViewAtVisibleRow(row);
        const int        y    = row * cellHeight_ + cellAscent_;
        for (int col = 0; col < grid.cols; ++col) {
            const ZzCellView cell = line.cellAt(col);
            painter.fillRect(col * cellWidth_, row * cellHeight_,
                             cellWidth_, cellHeight_,
                             resolveCellColor(cell.background, false));
            if (linkHighlight.isValid()
                && cell.hyperlinkId == hoverHyperlinkId_)
                painter.fillRect(col * cellWidth_, row * cellHeight_,
                                 cellWidth_, cellHeight_, linkHighlight);
        }
        for (int col = 0; col < grid.cols; ++col) {
            const ZzCellView cell = line.cellAt(col);
            const QColor fg = resolveCellColor(cell.foreground, true);
            if (linksActive && ctrlHeld_ && cell.hyperlinkId != 0) {
                // 下划线画在格底线（续格亦画，宽字符下划线不断线）
                painter.setPen(fg);
                const int uy = row * cellHeight_ + cellHeight_ - 1;
                painter.drawLine(col * cellWidth_, uy,
                                 (col + 1) * cellWidth_ - 1, uy);
            }
            if (cell.text.empty())
                continue;
            painter.setPen(fg);
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
        painter.fillRect(cursorRect, resolveCellColor(ZzColor::Default(), true));
        const ZzLineView cursorLine = view.lineAt(cursor.position.row);
        const ZzCellView under = cursorLine.cellAt(cursor.position.col);
        painter.setPen(resolveCellColor(ZzColor::Default(), false));
        const int textY = cursor.position.row * cellHeight_ + cellAscent_;
        if (under.width == ZzCellWidth::WideContinuation
            && cursor.position.col > 0) {
            // 光标落在宽字符续格（行尾 wrap-pending 或 CSI CUP 定位）：
            // 块盖住左格字形右半，从首格原点以反色重绘整字，右半笔画
            // 盖回光标块上（与窄字符「重绘该格文本」同口径）。
            const ZzCellView lead = cursorLine.cellAt(cursor.position.col - 1);
            if (!lead.text.empty())
                painter.drawText((cursor.position.col - 1) * cellWidth_, textY,
                                 QString::fromStdString(lead.text));
        } else if (!under.text.empty()) {
            painter.drawText(cursor.position.col * cellWidth_, textY,
                             QString::fromStdString(under.text));
        }
    }

    // IME preedit：组合中串即时绘制于光标处（下划线），仅钉底且光标可见时。
    if (!preedit_.isEmpty() && scrollOffset_ == 0) {
        const ZzCursorState imeCursor = term_.cursor();
        if (imeCursor.visible && imeCursor.position.row < view.size().rows) {
            QFont f = painter.font();
            f.setUnderline(true); // preedit 惯例样式：下划线
            painter.setFont(f);
            painter.setPen(scheme_.foreground);
            const int y = imeCursor.position.row * cellHeight_ + cellAscent_;
            // 从光标格起绘，超出右缘部分裁剪（不换行模式由横滚覆盖，不特殊处理）
            painter.setClipRect(rect());
            painter.drawText(imeCursor.position.col * cellWidth_, y, preedit_);
            painter.setClipping(false);
        }
    }
}

void ZzCoreTerminalWidget::paintUnwrapped(QPainter &painter)
{
    const ZzUnwrapView &view = term_.unwrapView();
    const ZzSize        grid = term_.size();
    // M22b：同 paintWrapped——Ctrl 按住全链接下划线，hover 同 id 段高亮
    const bool linksActive = ZzAppSettings::instance().hyperlinkEnabled();
    QColor linkHighlight;
    if (linksActive && hoverHyperlinkId_ != 0) {
        linkHighlight = palette().highlight().color();
        linkHighlight.setAlpha(60);
    }
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
        // 与 paintWrapped 同口径的两遍制：先背景后文本，双宽字符字形
        // 横跨首格与续格，单遍会被续格背景填充裁掉右半。
        for (int c = hScrollOffset_;
             c < line.cellCount() && c - hScrollOffset_ < grid.cols; ++c) {
            const ZzCellView cell = line.cellAt(c);
            painter.fillRect((c - hScrollOffset_) * cellWidth_, row * cellHeight_,
                             cellWidth_, cellHeight_,
                             resolveCellColor(cell.background, false));
            if (linkHighlight.isValid()
                && cell.hyperlinkId == hoverHyperlinkId_)
                painter.fillRect((c - hScrollOffset_) * cellWidth_,
                                 row * cellHeight_,
                                 cellWidth_, cellHeight_, linkHighlight);
        }
        for (int c = hScrollOffset_;
             c < line.cellCount() && c - hScrollOffset_ < grid.cols; ++c) {
            const ZzCellView cell = line.cellAt(c);
            const QColor fg = resolveCellColor(cell.foreground, true);
            if (linksActive && ctrlHeld_ && cell.hyperlinkId != 0) {
                painter.setPen(fg);
                const int uy = row * cellHeight_ + cellHeight_ - 1;
                painter.drawLine((c - hScrollOffset_) * cellWidth_, uy,
                                 (c - hScrollOffset_ + 1) * cellWidth_ - 1, uy);
            }
            if (cell.text.empty())
                continue;
            painter.setPen(fg);
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
            painter.fillRect(cursorRect, resolveCellColor(ZzColor::Default(), true));
            // IME preedit：与 paintWrapped 同样式（下划线），叠加于光标格起。
            if (!preedit_.isEmpty()) {
                QFont f = painter.font();
                f.setUnderline(true); // preedit 惯例样式：下划线
                painter.setFont(f);
                painter.setPen(scheme_.foreground);
                painter.setClipRect(rect());
                painter.drawText(col * cellWidth_,
                                 (int)row * cellHeight_ + cellAscent_,
                                 preedit_);
                painter.setClipping(false);
            }
        }
    }
}

// 选区高亮最小反馈：反色矩形覆盖重叠段后，以高亮前景重绘该段文本。
// 选区坐标为逻辑行空间（折链合并）；逻辑行号 == 拼接行索引、内容流偏移与
// 拼接列同口径（Core 的 zzLogicalLineCount 与 ZzUnwrapView 同规则分链），
// 物理坐标经 toStitched 换算后与 [start, end) 逐格求交。
void ZzCoreTerminalWidget::paintSelectionOnWrappedRow(QPainter &painter, int row)
{
    ZzLogicalPos start, end;
    if (!term_.selectionRange(start, end))
        return;
    const auto hist = (std::int64_t)term_.historyView().lineCount();
    const std::int64_t u = hist - scrollOffset_ + row; // 本可见行的统一物理行号
    const ZzSize grid = term_.size();
    const ZzUnwrapView &view = term_.unwrapView();
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
    painter.save(); // 高亮画笔不外泄——后续行的文本仍用 cell 前景色
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

void ZzCoreTerminalWidget::paintSelectionOnStitchedRow(QPainter &painter, std::size_t idx,
                                                       int row, const ZzStitchedLineView &line)
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
    painter.save(); // 高亮画笔不外泄——后续行的文本仍用 cell 前景色
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

void ZzCoreTerminalWidget::keyPressEvent(QKeyEvent *event)
{
    // M22b：跟踪 Ctrl 按住态——按住期间所有链接格画下划线（重绘即显）
    if (event->key() == Qt::Key_Control && !ctrlHeld_) {
        ctrlHeld_ = true;
        if (ZzAppSettings::instance().hyperlinkEnabled())
            update();
    }
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

void ZzCoreTerminalWidget::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty())
        term_.sendText(event->commitString().toUtf8().toStdString());
    // commit 与 preedit 可同事件携带（连续上屏时 preedit 已是下一拍），
    // 一律以事件值为准，空串即清除。
    preedit_ = event->preeditString();
    update();
    event->accept();
}

QVariant ZzCoreTerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle: {
        // 光标微焦点矩形（输入法候选窗定位）；滚动回看时上报现行光标格即可
        const ZzCursorState cursor = term_.cursor();
        return QRect(cursor.position.col * cellWidth_,
                     cursor.position.row * cellHeight_,
                     cellWidth_, cellHeight_);
    }
    case Qt::ImFont:
        return font();
    default:
        return QWidget::inputMethodQuery(query);
    }
}

void ZzCoreTerminalWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // event->size() 即现行 size()（Qt 先置尺寸再投递），共用对账块。
    recomputeGridForCurrentSize();
}

void ZzCoreTerminalWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 隐藏期字号变更不重算网格（隐藏期不重算是既定口径）；重新可见时若 Qt 不
    // 投递 resizeEvent（尺寸未变），网格会滞留旧 cell 口径——这里以现行
    // cell 尺寸对现行控件尺寸补一次对账（幂等：口径一致时重算早退）。
    recomputeGridForCurrentSize();
    update();
}

int ZzCoreTerminalWidget::maxScrollOffset() const noexcept
{
    if (unwrapMode_)
        return stitchedHistoryCount(); // 拼接空间：完全落在历史区的拼接行数
    return static_cast<int>(term_.historyView().lineCount()); // Alternate 屏契约恒 0
}

int ZzCoreTerminalWidget::stitchedHistoryCount() const
{
    const ZzUnwrapView &view = term_.unwrapView();
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

std::size_t ZzCoreTerminalWidget::stitchedTotalLines() const
{
    return term_.unwrapView().lineCount();
}

void ZzCoreTerminalWidget::setUnwrapMode(bool on)
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

int ZzCoreTerminalWidget::maxHScrollOffset() const noexcept
{
    if (!unwrapMode_)
        return 0;
    return qMax(0, term_.unwrapView().maxCellCount() - term_.size().cols);
}

void ZzCoreTerminalWidget::setHScrollOffset(int offset)
{
    offset = qBound(0, offset, maxHScrollOffset());
    if (offset == hScrollOffset_)
        return;
    hScrollOffset_ = offset;
    emit hScrollOffsetChanged(hScrollOffset_);
    update();
}

void ZzCoreTerminalWidget::followCursorHorizontally()
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

void ZzCoreTerminalWidget::setScrollOffset(int offset)
{
    offset = qBound(0, offset, maxScrollOffset());
    if (offset == scrollOffset_)
        return;
    scrollOffset_ = offset;
    emit scrollOffsetChanged(scrollOffset_);
    update();
}

ZzLineView ZzCoreTerminalWidget::lineViewAtVisibleRow(int row) const
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

QString ZzCoreTerminalWidget::visibleLineTextForTest(int row) const
{
    const ZzLineView line = lineViewAtVisibleRow(row);
    QString out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += QString::fromStdString(line.cellAt(col).text);
    return out;
}

void ZzCoreTerminalWidget::wheelEvent(QWheelEvent *event)
{
    // hi-res 滚轮（触控板/精密滚轮）单事件 delta 常不足一档 120：
    // 累计余数、凑满成档，整除截断不再吞掉小 delta。缩放/alt/普通
    // 三分支共用一个累加器（切换修饰键不清零，与 Konsole 同口径）。
    wheelRemainder_ += event->angleDelta().y();
    const int steps = wheelRemainder_ / 120;
    wheelRemainder_ -= steps * 120;

    if (event->modifiers() & Qt::ControlModifier) {
        if (steps != 0) {
            // Ctrl+滚轮缩放：6-72pt 钳制；设置页改动经 applySettings 重置缩放
            const int next = qBound(6, font().pointSize() + steps, 72);
            if (next != font().pointSize())
                setTerminalFontPointSize(next);
            event->accept();
            return;
        }
    }
    if (term_.isAlternateScreen()) {
        // Alternate 无历史：滚轮转方向键发给会话（vim/less 翻行），每档 3 行。
        // Z3 鼠标上报落地后此处需加「上报开启时不转」分支（登记，不在本任务）。
        if (steps != 0) {
            ZzKeyEvent keyEvent;
            keyEvent.action    = ZzKeyEvent::Action::Press;
            keyEvent.modifiers = ZzKeyModifier::None;
            keyEvent.key       = steps > 0 ? ZzKeyEvent::Key::Up
                                           : ZzKeyEvent::Key::Down;
            for (int i = 0, n = qAbs(steps) * 3; i < n; ++i)
                term_.sendKey(keyEvent);
            event->accept();
            return;
        }
        event->ignore();
        return;
    }
    if (steps != 0) {
        // offset 语义为离底行数：上滚（steps > 0）增大 offset 进入历史，每格 3 行。
        if (steps > 0 && maxScrollOffset() > 0 && scrollOffset_ >= maxScrollOffset()) {
            emit historyTopReached(); // 已到顶仍上滚：通知读回桥注入更早历史
        }
        setScrollOffset(scrollOffset_ + steps * 3);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void ZzCoreTerminalWidget::notifyHistoryPrepended(int prepended)
{
    // 注入使统一行号顺移 +prepended：拖拽中的锚点副本同步正移（Core 侧
    // selection 已由 prependHistoryLines 正移；此副本在下次 mouseMove
    // 全量重放，不正移会把选区盖回漂移位置）。非拖拽态无害（下次
    // mousePress 重算）。
    selAnchor_.line += prepended;
    emit scrollRangeChanged(maxScrollOffset()); // 上限增长，滚动条扩展
    setScrollOffset(scrollOffset_);             // 夹取（数值不变即视口稳定）
    update();
}

QString ZzCoreTerminalWidget::visibleStitchedTextForTest(int row) const
{
    const ZzUnwrapView &view = term_.unwrapView();
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

ZzLogicalPos ZzCoreTerminalWidget::logicalPosAtPixel(QPoint pos) const
{
    const int row = qBound(0, pos.y() / cellHeight_, term_.size().rows - 1);
    const int col = qBound(0, pos.x() / cellWidth_, term_.size().cols - 1);
    // 选区/搜索的 ZzLogicalPos 是逻辑行空间（折链合并为一条，col 为内容流
    // 偏移）；ZzUnwrapView 拼接坐标与其一一对应（拼接行索引 == 逻辑行号、
    // 拼接列 == 内容流偏移），物理统一坐标经 toStitched 换算。注意：
    // fromStitched 输出的是物理统一坐标，不能直接喂 setSelection。
    if (unwrapMode_) {
        const ZzUnwrapView &view = term_.unwrapView();
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

std::uint16_t ZzCoreTerminalWidget::hyperlinkIdAtPixel(QPoint pos, QString *uriOut) const
{
    if (!ZzAppSettings::instance().hyperlinkEnabled())
        return 0;
    const ZzSize grid = term_.size();
    const int row = pos.y() / cellHeight_;
    const int col = pos.x() / cellWidth_;
    // 不夹取：网格区外的空白/边沿像素不算命中（夹取会把边缘链接误报到屏外点）
    if (row < 0 || row >= grid.rows || col < 0 || col >= grid.cols)
        return 0;

    const auto hist = (std::int64_t)term_.historyView().lineCount();
    std::uint16_t id = 0;
    bool fromHistory = false;
    if (unwrapMode_) {
        // 拼接空间纵向视口（口径同 paintUnwrapped）；横向加 hScrollOffset_
        const ZzUnwrapView &view = term_.unwrapView();
        const auto total = (long long)view.lineCount();
        const long long idx = total - scrollOffset_ - grid.rows + row;
        if (idx < 0 || idx >= total)
            return 0;
        const ZzStitchedLineView line = view.lineAt((std::size_t)idx);
        const int c = col + hScrollOffset_;
        if (c >= line.cellCount())
            return 0;
        id = line.cellAt(c).hyperlinkId;
        // 跨缝拼接行归历史侧（sourceLine() < hist）。这是 hyperlink 命中分派的
        // 有意口径，与 :648 选区口径（sourceLine + sourceLineCount <= hist，
        // 跨缝归屏幕侧）本就不同——id→URI 池为终端级，两侧视图同池解析，
        // 归哪侧结果相同。
        fromHistory = line.sourceLine() < hist;
    } else {
        // 统一物理行号（口径同 lineViewAtVisibleRow）：[0,hist) 历史区
        const std::int64_t u = hist - scrollOffset_ + row;
        ZzCellView cell;
        if (u < hist) {
            const ZzLineView line = term_.historyView().lineAt((std::size_t)u);
            if (col >= line.cellCount())
                return 0;
            cell = line.cellAt(col);
            fromHistory = true;
        } else {
            const ZzLineView line = term_.renderView().lineAt((int)(u - hist));
            if (col >= line.cellCount())
                return 0;
            cell = line.cellAt(col);
        }
        id = cell.hyperlinkId;
    }
    if (id == 0)
        return 0;
    // id→URI 池是终端级的，历史/屏幕两视图同一池；按区域取主视图即可
    const std::string u8 = fromHistory
                               ? term_.historyView().hyperlinkUri(id)
                               : term_.renderView().hyperlinkUri(id);
    if (u8.empty())
        return 0; // LRU 淘汰/未登记：视为无链接
    if (uriOut)
        *uriOut = QString::fromStdString(u8);
    return id;
}

void ZzCoreTerminalWidget::mousePressEvent(QMouseEvent *event)
{
    // M22b：Ctrl+点击命中链接直接打开，不启动选区；落在非链接格照常走选区
    if (event->button() == Qt::LeftButton
        && (event->modifiers() & Qt::ControlModifier) != 0
        && ZzAppSettings::instance().hyperlinkEnabled()) {
        QString uri;
        if (hyperlinkIdAtPixel(event->pos(), &uri) != 0 && !uri.isEmpty()) {
            const QUrl url(uri);
            static const QStringList kAllowed = {
                QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("ftp"),
                QStringLiteral("ssh"), QStringLiteral("mailto"), QStringLiteral("file") };
            if (kAllowed.contains(url.scheme().toLower()))
                QDesktopServices::openUrl(url);
            event->accept();
            return;
        }
    }
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

void ZzCoreTerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    // M22b：Ctrl+悬停链接检测（选区拖拽逻辑之前；松开 Ctrl 后下一次
    // 移动以 ctrl=false 自然清 hover，leaveEvent/keyReleaseEvent 兜底）
    const bool ctrl = (event->modifiers() & Qt::ControlModifier) != 0;
    std::uint16_t newHover = 0;
    QString uri;
    if (ctrl && !selecting_)
        newHover = hyperlinkIdAtPixel(event->pos(), &uri);
    if (newHover != hoverHyperlinkId_) {
        hoverHyperlinkId_ = newHover;
        hoverHyperlinkUri_ = uri;
        setCursor(newHover != 0 ? Qt::PointingHandCursor : Qt::IBeamCursor);
        if (newHover != 0) {
            ZzFluentUI::ZzToolTip::showText(event->globalPosition().toPoint(), uri, this);
            // 临时诊断（ZZ_DEBUG_TOOLTIP=1 时输出 tooltip 配色链路，定位浅色主题文字发灰问题）
            if (qEnvironmentVariableIsSet("ZZ_DEBUG_TOOLTIP")) {
                QTimer::singleShot(120, this, [] {
                    const auto dumpPal = [](const char *tag, const QPalette &p) {
                        qInfo("[tipdbg] %s ToolTipText act=%s ina=%s dis=%s resolveMask=%#x",
                              tag,
                              qPrintable(p.color(QPalette::Active, QPalette::ToolTipText).name()),
                              qPrintable(p.color(QPalette::Inactive, QPalette::ToolTipText).name()),
                              qPrintable(p.color(QPalette::Disabled, QPalette::ToolTipText).name()),
                              p.resolveMask());
                    };
                    dumpPal("app    ", QApplication::palette());
                    for (QWidget *tlw : QApplication::topLevelWidgets()) {
                        if (tlw->objectName() == QLatin1String("ZzToolTipLabel")
                            && tlw->isVisible()) {
                            dumpPal("tiplbl ", tlw->palette());
                            qInfo("[tipdbg] tiplbl opacity=%g dpr=%g translucent=%d setPalette=%d font=%s %dpx",
                                  tlw->windowOpacity(),
                                  tlw->devicePixelRatioF(),
                                  tlw->testAttribute(Qt::WA_TranslucentBackground),
                                  tlw->testAttribute(Qt::WA_SetPalette),
                                  qPrintable(tlw->font().family()),
                                  tlw->font().pixelSize());
                        }
                    }
                });
            }
        } else {
            ZzFluentUI::ZzToolTip::hideText();
        }
        update(); // 触发重绘（下划线 + 关联高亮）
    }

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

void ZzCoreTerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (selecting_ && event->button() == Qt::LeftButton) {
        selecting_ = false;
        copySelectionToClipboard(); // 复制完整拼接行（含屏外）
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ZzCoreTerminalWidget::keyReleaseEvent(QKeyEvent *event)
{
    // M22b：Ctrl 抬起——清下划线态与 hover（光标/tooltip/高亮一并复位）
    if (event->key() == Qt::Key_Control) {
        ctrlHeld_ = false;
        if (hoverHyperlinkId_ != 0) {
            hoverHyperlinkId_ = 0;
            hoverHyperlinkUri_.clear();
            setCursor(Qt::IBeamCursor);
            ZzFluentUI::ZzToolTip::hideText();
        }
        update();
    }
    QWidget::keyReleaseEvent(event);
}

void ZzCoreTerminalWidget::leaveEvent(QEvent *event)
{
    // 鼠标离窗：清 hover（tooltip 不留残影，高亮随重绘消失）
    if (hoverHyperlinkId_ != 0) {
        hoverHyperlinkId_ = 0;
        hoverHyperlinkUri_.clear();
        setCursor(Qt::IBeamCursor);
        ZzFluentUI::ZzToolTip::hideText();
        update();
    }
    QWidget::leaveEvent(event);
}

void ZzCoreTerminalWidget::selectAll()
{
    // 尾端钳到最后一条非空拼接行：全量区间 [0, lineCount) 会把末尾
    // 空白屏行也选入，selectedText 对空白行逐行输出 '\n'（全选复制
    // 会带出一串空行）；钳后口径 = 全部有内容的逻辑行。
    const auto total = (std::int64_t)term_.unwrapView().lineCount();
    std::int64_t last = total;
    while (last > 0 && term_.unwrapView().lineAt((std::size_t)last - 1).cellCount() == 0)
        --last;
    if (last == 0) { // 全空：无可选内容
        term_.clearSelection();
        update();
        return;
    }
    term_.setSelection({0, 0}, {last, 0}); // 逻辑行空间半开区间
    update();
}

bool ZzCoreTerminalWidget::hasSelectionForMenu() const
{
    return term_.hasSelection();
}

void ZzCoreTerminalWidget::copySelectionToClipboard()
{
    if (!term_.hasSelection())
        return;
    const std::string text = term_.selectedText();
    if (!text.empty())
        QGuiApplication::clipboard()->setText(QString::fromStdString(text));
}

void ZzCoreTerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copy = menu.addAction(tr("复制"));
    copy->setEnabled(hasSelectionForMenu());
    QAction *paste = menu.addAction(tr("粘贴"));
    paste->setEnabled(!QGuiApplication::clipboard()->text().isEmpty());
    QAction *all = menu.addAction(tr("全选"));
    menu.addSeparator();
    QAction *clear = menu.addAction(tr("清空屏幕"));
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == copy) {
        copySelectionToClipboard();
    } else if (chosen == paste) {
        term_.sendText(QGuiApplication::clipboard()->text().toUtf8().toStdString());
    } else if (chosen == all) {
        selectAll();
    } else if (chosen == clear) {
        // 本地清屏（ED3+ED2+Home）：shell 不感知，与 fork 时代 Clear 同口径
        recvData(QByteArray("\x1b[3J\x1b[2J\x1b[H"));
    }
    event->accept(); // 菜单已处置（含取消）：阻断向父控件传播
}
