#pragma once

/**
 * @file ZzCoreTerminalWidget.h
 * @brief Z1：ZzTermCore RenderView/UnwrapView 契约的 Qt Widget 前端
 * （spike ZzCoreViewWidget 升格 + 默认调色板配色）。
 *
 * 与 spike 原型的差异：
 * - 喂入口更名 recvData(QByteArray)（对齐 ZzTerminalView 胶水用语）；
 * - 键盘字节经信号 sendData(QByteArray) 发出（spike 是 writeFn 回调）；
 * - 渲染带配色：cell fg/bg 经 ZzTerminalPalette 当前方案（默认
 *   defaultSchemeName()，setColorScheme 运行时切换）解析为 QColor，
 *   背景格填充；
 * - 暴露 gridCols()/gridRows()（ZzTabManager 的 screenColumnsCount/
 *   screenLinesCount 等价物）与 cellWidthForTest/cellHeightForTest；
 * - 保留 spike 的：不换行模式（Ctrl+Shift+U）、选区（锚点自存重放、
 *   画笔收口、端点粒度+1、越缘步进）、留痕（ZZCORE_SPIKE_TRACE）。
 */

#include <QByteArray>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <cstdio>
#include <string_view>

#include <ZzTerm/Terminal.h>

#include "ZzTerminalPalette.h"

class QContextMenuEvent;
class QInputMethodEvent;
class QPainter;
class ZzStitchedLineView;

/**
 * @brief ZzTermCore 引擎的 Qt Widget 前端（Z1 最小可用形态）。
 */
class ZzCoreTerminalWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ZzCoreTerminalWidget(QWidget *parent = nullptr);

    /** @brief 远端输出喂入（渲染方向唯一入口）。 */
    void recvData(const QByteArray &data);

    /** @brief 只读访问持有的终端（测试与视图层坐标换算用）。 */
    [[nodiscard]] const ZzTerminal &terminal() const noexcept { return term_; }
    /** @brief 可变访问（测试设置选区/尺寸用）。 */
    [[nodiscard]] ZzTerminal &terminal() noexcept { return term_; }

    /** @brief 当前网格列数/行数（screenColumnsCount/screenLinesCount 等价物）。 */
    [[nodiscard]] int gridCols() const noexcept { return term_.size().cols; }
    [[nodiscard]] int gridRows() const noexcept { return term_.size().rows; }

    /** @brief 不换行显示模式开关（M17b；Ctrl+Shift+U 亦切换）。 */
    void setUnwrapMode(bool on);
    [[nodiscard]] bool unwrapMode() const noexcept { return unwrapMode_; }

    /** @brief 横向滚动（不换行模式）。 */
    void setHScrollOffset(int offset);
    [[nodiscard]] int hScrollOffset() const noexcept { return hScrollOffset_; }
    [[nodiscard]] int maxHScrollOffset() const noexcept;

    /** @brief 纵向滚动偏移（0 = 底部最新）。 */
    void setScrollOffset(int offset);
    [[nodiscard]] int scrollOffset() const noexcept { return scrollOffset_; }
    [[nodiscard]] int maxScrollOffset() const noexcept;

    /** @brief 测试钩子：可见第 row 行文本（物理模式）。 */
    [[nodiscard]] QString visibleLineTextForTest(int row) const;
    /** @brief 测试钩子：不换行模式可见第 row 行横向窗口文本。 */
    [[nodiscard]] QString visibleStitchedTextForTest(int row) const;
    /** @brief 测试钩子：单格像素宽/高。 */
    [[nodiscard]] int cellWidthForTest() const noexcept { return cellWidth_; }
    [[nodiscard]] int cellHeightForTest() const noexcept { return cellHeight_; }

    /** @brief 设置终端字号（pt）。 */
    void setTerminalFontPointSize(int pointSize);

    /** @brief 切换配色方案（未知名回退默认）；即时生效（重绘）。 */
    void setColorScheme(const QString &name);
    /** @brief 当前方案名（已回退后的实际值）。 */
    [[nodiscard]] QString colorSchemeName() const noexcept { return schemeName_; }

    /** @brief 输入法查询（候选窗定位微焦点等；QWidget 为 public virtual）。 */
    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    /** @brief 测试钩子：IME 组合中串（preedit，不进会话）。 */
    [[nodiscard]] QString preeditTextForTest() const { return preedit_; }

    /** @brief 全选（逻辑行空间 [0,0) → 最后一条非空拼接行+1 半开区间；
     *  尾部空白屏行不计入，全空时清空选区）。 */
    void selectAll();
    /** @brief 复制当前选区到剪贴板（右键菜单与鼠标释放共用）。 */
    void copySelectionToClipboard();
    /** @brief 是否存在非空选区（菜单项可用性）。 */
    [[nodiscard]] bool hasSelectionForMenu() const;

signals:
    /** @brief 键盘编码字节（终端→传输方向）。 */
    void sendData(const QByteArray &data);
    /** @brief 网格行列变化（宿主据此去抖转发 transport->resize）。 */
    void gridResized(int cols, int rows);
    /** @brief 纵向可滚动范围/偏移变化（ZzTerminalView 滚动条接线用）。 */
    void scrollRangeChanged(int maximum);
    void scrollOffsetChanged(int offset);
    /** @brief 横向可滚动范围/偏移变化。 */
    void hScrollRangeChanged(int maximum);
    void hScrollOffsetChanged(int offset);
    /** @brief 一次 recvData 的 feed 完成（历史落盘差分等观察者用）。 */
    void contentFed();
    /** @brief 用户上滚已到历史最顶端（读回桥注入触发点，规格 §4.4）。 */
    void historyTopReached();

public:
    /** @brief 读回桥注入完成后调用：重发射滚动范围/夹取偏移/重绘。 */
    void notifyHistoryPrepended();
    /** @brief 外部触发点（滚动条拖到顶等）：发射 historyTopReached 请求读回注入。 */
    void requestOlderHistory() { emit historyTopReached(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    // —— 以下成员与 spike ZzCoreViewWidget 同名同义（含三个手测修复）——
    void updateCellMetrics();
    void paintWrapped(QPainter &painter);
    void paintUnwrapped(QPainter &painter);
    void paintSelectionOnWrappedRow(QPainter &painter, int row);
    void paintSelectionOnStitchedRow(QPainter &painter, std::size_t idx,
                                     int row, const ZzStitchedLineView &line);
    [[nodiscard]] ZzLineView lineViewAtVisibleRow(int row) const;
    [[nodiscard]] ZzLogicalPos logicalPosAtPixel(QPoint pos) const;
    [[nodiscard]] std::size_t stitchedTotalLines() const;
    [[nodiscard]] int stitchedHistoryCount() const;
    void followCursorHorizontally();
    void traceFeed(std::string_view data);
    void traceResize(int cols, int rows);
    /** @brief 引擎 ZzColor 经当前方案解析为 QColor（替代 Z1 自由函数）。 */
    [[nodiscard]] QColor resolveCellColor(ZzColor color, bool isForeground) const;

    ZzTerminal term_;                 ///< 引擎（Native 80x24，历史 10000）
    ZzTerminalPalette::Scheme scheme_;   ///< 当前方案（构造即默认方案）
    QString schemeName_;                 ///< 当前方案名（回退后实际值）
    int cellWidth_ = 1;
    int cellHeight_ = 1;
    int cellAscent_ = 0;
    bool unwrapMode_ = false;
    int hScrollOffset_ = 0;
    int scrollOffset_ = 0;
    bool selecting_ = false;
    ZzLogicalPos selAnchor_{};
    QString preedit_;                 ///< IME 组合中串（绘制于光标处，不进会话）
    std::FILE *trace_ = nullptr;      ///< 留痕（env ZZCORE_SPIKE_TRACE）
};
