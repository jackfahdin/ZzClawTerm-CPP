#pragma once

#include <QByteArray>
#include <QString>

/**
 * @brief 滚动历史中的一行：纯文本 + 不透明的字符属性负载。
 *
 * attributes 承载颜色 / 粗体等终端层字符属性；日志引擎只做不透明存储与
 * 回读，不解析内容，也不绑定任一终端前端实现。
 */
struct ZzLogLine {
    QString text;          ///< 行纯文本（不含换行符）
    QByteArray attributes; ///< 字符属性负载（可为空）
};
