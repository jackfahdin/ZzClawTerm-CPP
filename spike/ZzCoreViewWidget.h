#pragma once

/**
 * @file ZzCoreViewWidget.h
 * @brief M13 spike：ZzTermCore ZzRenderView 契约的最小 Qt Widget 前端。
 *
 * 职责：
 * - 持有一个 ZzTerminal（Native 后端，80x24，历史 1 万行），
 *   作为 feed -> renderView -> paint 全链的唯一宿主；
 * - 对外暴露单一喂入口 feedForTest()（QTest 与宿主 PTY 回调共用）；
 * - 键盘事件映射为 ZzKeyEvent / 文本，经 ZzTerminal::sendText/sendKey
 *   编码后由 writeFn_ 回调写出（编码经 Terminal 的 output 通道，
 *   自动联动 application cursor 等终端模式）；
 * - 尺寸变化按格宽格高换算行列，调 ZzTerminal::resize 并发出
 *   gridResized 信号供宿主转发 PTY/通道 window-change。
 *
 * 本 widget 是 spike 产物：全量重绘、无增量、无配色（背景/文本默认色）。
 * - 历史滚动（M14）：滚轮/滚动条偏移经 ZzHistoryView 拼接历史段与屏幕段
 *   绘制；键盘输入自动回底；Alternate 屏（vim 等）滚动禁用。
 * - 不换行显示（M17b）：Ctrl+Shift+U 切换——经 ZzUnwrapView 拼接行渲染 +
 *   横向滚动（光标贴边跟随）；鼠标拖拽选区（两模式反色高亮），松开复制
 *   完整拼接行文本到剪贴板。
 */

#include <QWidget>
#include <QString>
#include <QPoint>

#include <cstdio>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include <ZzTerm/Terminal.h>

class QPainter;
class ZzStitchedLineView;

/**
 * @brief ZzTermCore 渲染视图契约的最小 Qt Widget 验证件。
 */
class ZzCoreViewWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造 widget 与其持有的终端（80x24、Native 后端、历史 10000 行）。
     * @param parent 父 widget。
     */
    explicit ZzCoreViewWidget(QWidget* parent = nullptr);

    /**
     * @brief 设置输入字节写出回调（键盘编码结果经此发往 PTY/连接）。
     * @param fn 写出回调；为空则编码字节静默丢弃。
     */
    void setWriteFn(std::function<void(std::string)> fn);

    /**
     * @brief 向终端喂入原始字节流并触发重绘（QTest 与宿主共用的单一 feed 入口）。
     * @param data 原始字节流（可在 UTF-8 或控制序列中间截断，Core 保存续接状态）。
     */
    void feedForTest(std::string_view data);

    /**
     * @brief 只读访问持有的终端（测试断言 renderView 文本用）。
     * @return 终端常量引用。
     */
    [[nodiscard]] const ZzTerminal& terminal() const noexcept { return term_; }

    /**
     * @brief 可变访问持有的终端（测试调 resize/setSelection 等非常量接口用）。
     * @return 终端引用。
     */
    [[nodiscard]] ZzTerminal& terminal() noexcept { return term_; }

    /**
     * @brief 设置滚动偏移（0 = 底部最新；向上滚动查看历史的行数）。
     * @param offset 目标偏移，自动夹取到 [0, maxScrollOffset()]。
     */
    void setScrollOffset(int offset);

    /**
     * @brief 当前滚动偏移。
     * @return 0 表示底部。
     */
    [[nodiscard]] int scrollOffset() const noexcept { return scrollOffset_; }

    /**
     * @brief 最大滚动偏移（即可读历史行数；Alternate 屏契约恒 0）。
     * @return 历史行数。
     */
    [[nodiscard]] int maxScrollOffset() const noexcept;

    /**
     * @brief 测试钩子：可见第 row 行拼接后的纯文本（历史段+屏幕段统一视口）。
     * @param row 可见行号（0 起）。
     * @return 该行全部格文本拼接。
     */
    [[nodiscard]] QString visibleLineTextForTest(int row) const;

    /**
     * @brief 测试钩子：按滚轮格数滚动（wheelEvent 共用的同一逻辑）。
     * @param steps 滚轮格数（angleDelta().y()/120；正 = 上滚进入历史，每格 3 行）。
     */
    void scrollByWheelStepsForTest(int steps);

    /**
     * @brief 设置不换行显示模式（M17b）：ON = 拼接行渲染 + 横向滚动。
     * @param on true 进不换行模式；false 回物理行渲染（现状）。
     */
    void setUnwrapMode(bool on);

    /**
     * @brief 当前是否不换行显示模式。
     * @return 是则为 true。
     */
    [[nodiscard]] bool unwrapMode() const noexcept { return unwrapMode_; }

    /**
     * @brief 设置横向滚动偏移（列数，0 = 左端；仅不换行模式有效）。
     * @param offset 目标偏移，自动夹取到 [0, maxHScrollOffset()]。
     */
    void setHScrollOffset(int offset);

    /**
     * @brief 当前横向滚动偏移。
     * @return 列偏移（0 = 左端）。
     */
    [[nodiscard]] int hScrollOffset() const noexcept { return hScrollOffset_; }

    /**
     * @brief 最大横向滚动偏移 = max(0, maxCellCount - 可见列数)。
     * @return 列数上限。
     */
    [[nodiscard]] int maxHScrollOffset() const noexcept;

    /**
     * @brief 测试钩子：不换行模式下可见第 row 行的横向窗口文本。
     * @param row 可见行号（0 起）。
     * @return 该拼接行 [hOffset, hOffset+cols) 窗口内文本拼接。
     */
    [[nodiscard]] QString visibleStitchedTextForTest(int row) const;

    /**
     * @brief 测试钩子：不换行模式下可见拼接行数（含历史滚动的统一拼接空间）。
     * @return 当前视口可见的拼接行条数。
     */
    [[nodiscard]] int visibleStitchedCountForTest() const;

    /**
     * @brief 建议尺寸（80x24 网格按当前格宽格高换算的像素尺寸）。
     * @return 建议像素尺寸。
     */
    [[nodiscard]] QSize sizeHint() const override;

signals:
    /**
     * @brief 网格行列数变化（宿主据此转发 PTY resize / SSH window-change）。
     * @param cols 新列数。
     * @param rows 新行数。
     */
    void gridResized(int cols, int rows);

    /**
     * @brief 可滚动范围变化（历史行数变化；宿主据此更新滚动条上限）。
     * @param maximum 新的最大滚动偏移。
     */
    void scrollRangeChanged(int maximum);

    /**
     * @brief 滚动偏移变化（宿主据此同步滚动条位置）。
     * @param offset 新偏移（0 = 底部）。
     */
    void scrollOffsetChanged(int offset);

    /**
     * @brief 横向可滚动范围变化（宿主据此更新横向滚动条上限）。
     * @param maximum 新的最大横向偏移。
     */
    void hScrollRangeChanged(int maximum);

    /**
     * @brief 横向滚动偏移变化（宿主据此同步横向滚动条位置）。
     * @param offset 新偏移（0 = 左端）。
     */
    void hScrollOffsetChanged(int offset);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    /**
     * @brief 按当前等宽字体更新格宽格高度量。
     */
    void updateCellMetrics();

    /**
     * @brief 统一坐标取行：历史区在前、屏幕区在后，视口底部对齐。
     * @param row 可见行号（0 起）。
     * @return 该行只读句柄（当帧有效）。
     */
    [[nodiscard]] ZzLineView lineViewAtVisibleRow(int row) const;

    /**
     * @brief 操作留痕（ZZCORE_SPIKE_TRACE 启用时）：记录一次 feed。
     * @param data 喂入的原始字节。
     */
    void traceFeed(std::string_view data);

    /**
     * @brief 操作留痕（ZZCORE_SPIKE_TRACE 启用时）：记录一次网格 resize。
     * @param cols 新列数。
     * @param rows 新行数。
     */
    void traceResize(int cols, int rows);

    /**
     * @brief 物理行渲染（M13 现状分支，paintEvent 抽出的原实现）。
     * @param painter 当前帧画家。
     */
    void paintWrapped(QPainter& painter);

    /**
     * @brief 不换行模式渲染（M17b）：一个拼接行占一个显示行 + 横向窗口。
     * @param painter 当前帧画家。
     */
    void paintUnwrapped(QPainter& painter);

    /**
     * @brief 物理模式可见行 row 的选区反色高亮（无选区/无交叠则不动）。
     * @param painter 当前帧画家。
     * @param row 可见行号（0 起）。
     */
    void paintSelectionOnWrappedRow(QPainter& painter, int row);

    /**
     * @brief 不换行模式拼接行 idx（显示行 row）的选区反色高亮。
     * @param painter 当前帧画家。
     * @param idx 拼接行索引。
     * @param row 可见行号（0 起）。
     * @param line 该拼接行句柄（当帧有效）。
     */
    void paintSelectionOnStitchedRow(QPainter& painter, std::size_t idx, int row,
                                     const ZzStitchedLineView& line);

    /**
     * @brief 不换行模式下的统一拼接空间行数（历史段 + 屏幕段拼接后）。
     * @return 拼接行总数。
     */
    [[nodiscard]] std::size_t stitchedTotalLines() const;

    /**
     * @brief 不换行模式下完全落在历史区的拼接行数（跨缝链归屏幕侧）。
     * @return 历史拼接行数（纵向滚动上限）。
     */
    [[nodiscard]] int stitchedHistoryCount() const;

    /**
     * @brief 显示像素坐标 → 引擎统一坐标（两模式分派）。
     * @param pos 像素坐标。
     * @return 统一空间 ZzLogicalPos。
     */
    [[nodiscard]] ZzLogicalPos logicalPosAtPixel(QPoint pos) const;

    /**
     * @brief 不换行模式：光标拼接列出横向视口时贴边跟随。
     */
    void followCursorHorizontally();

    ZzTerminal                      term_;     ///< 终端模拟器（Native 后端）。
    std::function<void(std::string)> writeFn_; ///< 输入字节写出回调。
    int                             cellWidth_  = 1; ///< 单格像素宽。
    int                             cellHeight_ = 1; ///< 单格像素高。
    int                             cellAscent_ = 0; ///< 基线 ascent（drawText 用）。
    int scrollOffset_ = 0; ///< 滚动偏移（0 = 底部最新）。
    bool unwrapMode_ = false;   ///< 不换行显示模式（M17b）。
    int  hScrollOffset_ = 0;    ///< 横向滚动偏移（列，0 = 左端）。
    bool selecting_ = false;    ///< 鼠标拖拽选区进行中。
    std::FILE* trace_ = nullptr; ///< 留痕文件（env ZZCORE_SPIKE_TRACE 指定）。
};
