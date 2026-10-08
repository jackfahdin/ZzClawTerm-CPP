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
 */

#include <QWidget>
#include <QString>

#include <cstdio>
#include <functional>
#include <string>
#include <string_view>

#include <ZzTerm/Terminal.h>

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

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

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

    ZzTerminal                      term_;     ///< 终端模拟器（Native 后端）。
    std::function<void(std::string)> writeFn_; ///< 输入字节写出回调。
    int                             cellWidth_  = 1; ///< 单格像素宽。
    int                             cellHeight_ = 1; ///< 单格像素高。
    int                             cellAscent_ = 0; ///< 基线 ascent（drawText 用）。
    int scrollOffset_ = 0; ///< 滚动偏移（0 = 底部最新）。
    std::FILE* trace_ = nullptr; ///< 留痕文件（env ZZCORE_SPIKE_TRACE 指定）。
};
