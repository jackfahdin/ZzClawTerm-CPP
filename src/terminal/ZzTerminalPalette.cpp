#include "ZzTerminalPalette.h"

#include <QDir>
#include <QFile>
#include <QHash>

// ZzClawTermApp 是静态库：AUTORCC 生成的资源对象若无符号引用会被链接器
// 丢弃，导致 :/colorschemes/ 不可用。显式 Q_INIT_RESOURCE 建立引用并注册
//（Qt 文档对静态库内资源的标准做法）。
static const bool zzColorSchemesResourceRegistered = [] {
    Q_INIT_RESOURCE(colorschemes);
    return true;
}();

namespace {

struct RawScheme {
    QColor ansi[16];
    QColor foreground;
    QColor background;
    bool hasAnsi[16] = {};
    bool hasFg = false;
    bool hasBg = false;
};

[[nodiscard]] bool parseColor(const QString &text, QColor &out)
{
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != 3)
        return false;
    bool ok[3];
    const int r = parts[0].trimmed().toInt(&ok[0]);
    const int g = parts[1].trimmed().toInt(&ok[1]);
    const int b = parts[2].trimmed().toInt(&ok[2]);
    if (!ok[0] || !ok[1] || !ok[2])
        return false;
    out = QColor(r, g, b);
    return out.isValid();
}

/** @brief 解析单个 .colorscheme（qrc 路径）；整体读取失败返回 false。 */
[[nodiscard]] bool parseSchemeFile(const QString &name, RawScheme &out)
{
    QFile file(QStringLiteral(":/colorschemes/") + name
               + QStringLiteral(".colorscheme"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QString section;
    while (!file.atEnd()) {
        const QString line =
            QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';'))
            || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        if (line.left(eq) != QLatin1String("Color"))
            continue; // 只关心 Color= 键，其余（Blur 等）忽略
        QColor color;
        if (!parseColor(line.mid(eq + 1), color))
            continue;
        if (section == QLatin1String("Background")) {
            out.background = color;
            out.hasBg = true;
        } else if (section == QLatin1String("Foreground")) {
            out.foreground = color;
            out.hasFg = true;
        } else {
            // Color0..Color7 → ansi[0..7]；Color0Intense.. → ansi[8..15]
            int idx = -1;
            if (section.startsWith(QLatin1String("Color"))) {
                QString rest = section.mid(5);
                const bool intense = rest.endsWith(QLatin1String("Intense"));
                if (intense)
                    rest.chop(7);
                bool ok = false;
                const int n = rest.toInt(&ok);
                if (ok && n >= 0 && n <= 7)
                    idx = intense ? 8 + n : n;
            }
            if (idx >= 0) {
                out.ansi[idx] = color;
                out.hasAnsi[idx] = true;
            }
        }
    }
    return true;
}

/** @brief 方案缓存（字段级回退：缺失字段取默认方案对应值）。 */
[[nodiscard]] ZzTerminalPalette::Scheme loadScheme(const QString &name)
{
    static QHash<QString, ZzTerminalPalette::Scheme> cache;
    const auto it = cache.constFind(name);
    if (it != cache.constEnd())
        return it.value();

    ZzTerminalPalette::Scheme result;
    RawScheme raw;
    const bool ok = parseSchemeFile(name, raw);
    if (!ok && name != ZzTerminalPalette::defaultSchemeName())
        return loadScheme(ZzTerminalPalette::defaultSchemeName()); // 未知名整体回退
    if (!ok) {
        // 默认方案自身损坏（不应发生）：保底硬编码黑底白字
        result.foreground = QColor(0xcc, 0xcc, 0xcc);
        result.background = QColor(0x00, 0x00, 0x00);
        for (int i = 0; i < 16; ++i)
            result.ansi[i] = result.foreground;
        cache.insert(name, result);
        return result;
    }
    const ZzTerminalPalette::Scheme dflt =
        name == ZzTerminalPalette::defaultSchemeName()
            ? ZzTerminalPalette::Scheme{}
            : loadScheme(ZzTerminalPalette::defaultSchemeName());
    // 默认方案字段缺失时的最终保底（字段级回退链的最后一环）
    ZzTerminalPalette::Scheme fallback = dflt;
    if (name == ZzTerminalPalette::defaultSchemeName()) {
        fallback.foreground = QColor(0xcc, 0xcc, 0xcc);
        fallback.background = QColor(0x00, 0x00, 0x00);
        for (int i = 0; i < 16; ++i)
            fallback.ansi[i] = fallback.foreground;
    }
    result.foreground = raw.hasFg ? raw.foreground : fallback.foreground;
    result.background = raw.hasBg ? raw.background : fallback.background;
    for (int i = 0; i < 16; ++i)
        result.ansi[i] = raw.hasAnsi[i] ? raw.ansi[i] : fallback.ansi[i];
    cache.insert(name, result);
    return result;
}

} // namespace

QStringList ZzTerminalPalette::availableSchemes()
{
    QStringList out;
    const QDir dir(QStringLiteral(":/colorschemes"));
    const QStringList files = dir.entryList({QStringLiteral("*.colorscheme")},
                                            QDir::Files);
    out.reserve(files.size());
    for (const QString &f : files)
        out.append(f.left(f.size() - 12)); // 去 ".colorscheme" 后缀
    out.sort(Qt::CaseInsensitive);
    return out;
}

QString ZzTerminalPalette::defaultSchemeName()
{
    return QStringLiteral("Builtin Tango Dark");
}

ZzTerminalPalette::Scheme ZzTerminalPalette::scheme(const QString &name)
{
    return loadScheme(name);
}

QColor ZzTerminalPalette::colorForIndex(const Scheme &scheme, int idx)
{
    if (idx >= 0 && idx < 16)
        return scheme.ansi[idx];
    if (idx >= 16 && idx < 232) {
        const int v = idx - 16;
        const int r = v / 36, g = (v % 36) / 6, b = v % 6;
        auto lv = [](int c) { return c == 0 ? 0 : 55 + c * 40; };
        return QColor(lv(r), lv(g), lv(b));
    }
    if (idx >= 232 && idx < 256) {
        const int gray = 8 + (idx - 232) * 10;
        return QColor(gray, gray, gray);
    }
    return scheme.foreground;
}
