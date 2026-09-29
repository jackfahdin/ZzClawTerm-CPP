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
 * 本 widget 是 spike 产物：全量重绘、无增量、无配色（背景/文本默认色）、
 * 无历史滚动（RenderView 无历史行访问件，见任务 1 报告 API 缺口登记）。
 */

#include <QWidget>

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

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /**
     * @brief 按当前等宽字体更新格宽格高度量。
     */
    void updateCellMetrics();

    ZzTerminal                      term_;     ///< 终端模拟器（Native 后端）。
    std::function<void(std::string)> writeFn_; ///< 输入字节写出回调。
    int                             cellWidth_  = 1; ///< 单格像素宽。
    int                             cellHeight_ = 1; ///< 单格像素高。
    int                             cellAscent_ = 0; ///< 基线 ascent（drawText 用）。
};
