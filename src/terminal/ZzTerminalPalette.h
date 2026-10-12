#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

/**
 * @brief 终端配色方案表（Z2）：Konsole .colorscheme 解析 + 全量方案枚举。
 *
 * 数据来源：qrc:/colorschemes/（iTerm2-Color-Schemes vendored 快照，
 * 见 colorschemes/PROVENANCE.md）。零 Qt Widget 依赖，可独立 QTest。
 * 未知名/解析失败/字段缺失一律回退默认方案对应值，不抛错。
 */
class ZzTerminalPalette
{
public:
    struct Scheme {
        QColor ansi[16];     ///< 0-7 普通 + 8-15 亮（Color0Intense..Color7Intense）
        QColor foreground;   ///< 默认前景（引擎 ZzColor::Default 前景解析值）
        QColor background;   ///< 默认背景
    };

    /** @brief 全部可用方案名（字母序；恒含 defaultSchemeName()）。 */
    [[nodiscard]] static QStringList availableSchemes();

    /** @brief 默认方案名（settings.ini 缺省与未知名回退目标）。 */
    [[nodiscard]] static QString defaultSchemeName();

    /** @brief 按名取方案；未知名或解析失败回退默认方案。 */
    [[nodiscard]] static Scheme scheme(const QString &name);

    /**
     * @brief 引擎 Indexed 色解析：0-15 查方案表，16-231 xterm 立方体，
     *        232-255 灰阶，越界回退 scheme.foreground。
     */
    [[nodiscard]] static QColor colorForIndex(const Scheme &scheme, int idx);
};
