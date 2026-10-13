#include "ZzTerminalView.h"

#include <atomic>

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringConverter>
#include <QtCore5Compat/QTextCodec>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>

#include "terminal/ZzCoreTerminalWidget.h"
#include "log/ZzLogEngine.h"
#include "settings/ZzAppSettings.h"
#include "terminal/ZzScrollbackBridge.h"

ZzTerminalView::ZzTerminalView(QWidget *parent)
    : QWidget(parent)
{
    m_term = new ZzCoreTerminalWidget(this);
    setFocusProxy(m_term); // 视图级 setFocus（分屏焦点切换）落到终端组件
    // 最小终端网格约束（fork setMinimumTerminalSize(24,6) 等价物）已由
    // ZzCoreTerminalWidget 自带：minimumSizeHint + 网格重算钳制 24×6（Z2 任务 8）。

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 标签内错误横幅：默认隐藏（规格 §八：错误走标签内提示，不弹窗轰炸）
    m_errorBanner = new QWidget(this);
    m_errorBanner->setObjectName(QStringLiteral("zzErrorBanner"));
    m_errorBanner->setStyleSheet(
        QStringLiteral("#zzErrorBanner { background: #4a2b2b; color: #ffd7d7; }"));
    auto *bannerLayout = new QHBoxLayout(m_errorBanner);
    bannerLayout->setContentsMargins(8, 4, 8, 4);
    m_errorLabel = new QLabel(m_errorBanner);
    m_errorLabel->setWordWrap(true);
    m_retryButton = new QPushButton(m_errorBanner);
    m_retryButton->setObjectName(QStringLiteral("zzRetryButton"));
    bannerLayout->addWidget(m_errorLabel, 1);
    bannerLayout->addWidget(m_retryButton);
    m_errorBanner->hide();
    connect(m_retryButton, &QPushButton::clicked, this, [this]() {
        hideErrorBanner();
        if (m_transport) {
            m_transport->open(m_lastEndpoint); // 用记忆的参数重试
        }
    });
    layout->addWidget(m_errorBanner);

    // 终端 + 滚动条网格（Z2 任务 5）：纵条常驻，横条仅不换行模式出现
    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    m_vbar = new QScrollBar(Qt::Vertical, this);
    m_hbar = new QScrollBar(Qt::Horizontal, this);
    m_hbar->hide(); // 仅不换行模式出现
    grid->addWidget(m_term, 0, 0);
    grid->addWidget(m_vbar, 0, 1);
    grid->addWidget(m_hbar, 1, 0);
    layout->addLayout(grid, 1);

    // 滚动条 ⇄ widget 双向接线（值域即 scrollOffset：0=底部最新）
    connect(m_term, &ZzCoreTerminalWidget::scrollRangeChanged,
            m_vbar, &QScrollBar::setMaximum);
    connect(m_term, &ZzCoreTerminalWidget::scrollOffsetChanged,
            m_vbar, &QScrollBar::setValue);
    connect(m_vbar, &QScrollBar::valueChanged, this, [this](int value) {
        if (value == m_vbar->maximum() && value > 0)
            m_term->requestOlderHistory(); // 拖到顶同样触发读回注入
        m_term->setScrollOffset(value);
    });
    connect(m_term, &ZzCoreTerminalWidget::hScrollRangeChanged, this,
            [this](int max) {
                m_hbar->setMaximum(max);
                m_hbar->setVisible(max > 0);
            });
    connect(m_term, &ZzCoreTerminalWidget::hScrollOffsetChanged,
            m_hbar, &QScrollBar::setValue);
    connect(m_hbar, &QScrollBar::valueChanged,
            m_term, &ZzCoreTerminalWidget::setHScrollOffset);

    retranslateUi();

    // 终端 → 传输（键盘输入方向，经转码层：UTF-8 → 会话编码）
    connect(m_term, &ZzCoreTerminalWidget::sendData, this,
            [this](const QByteArray &data) {
                if (m_transport) {
                    const QString text = QString::fromUtf8(data);
                    m_transport->write(
                        m_codecEncoder
                            ? m_codecEncoder->fromUnicode(text.constData(),
                                                          int(text.size()))
                            : m_encoder.encode(text));
                }
            });
    // 终端尺寸 → 传输（尾随去抖 150ms：拖拽期间只记最终尺寸，停手后一次送达，
    // 避免远端按过期几何批量重绘产生显示残迹）+ 状态栏（即时刷新）
    connect(&m_resizeDebouncer, &ZzResizeDebouncer::settled, this,
            [this](int cols, int rows) {
                if (m_transport) {
                    m_transport->resize(cols, rows);
                }
            });
    connect(m_term, &ZzCoreTerminalWidget::gridResized, this,
            [this](int cols, int rows) {
                m_resizeDebouncer.post(cols, rows);
                emit sizeChanged(cols, rows);
            });

    applySettings(ZzAppSettings::instance());
}

ZzTerminalView::~ZzTerminalView() = default;

void ZzTerminalView::setTransport(ZzTransportInterface *transport)
{
    if (m_transport) {
        disconnect(m_transport, nullptr, this, nullptr);
    }
    m_transport = transport;
    if (!m_transport) {
        return;
    }
    // 传输 → 终端（远端输出方向，经转码层：会话编码 → UTF-8）
    connect(m_transport, &ZzTransportInterface::dataReceived, this,
            [this](const QByteArray &data) {
                const QString text =
                    m_codecDecoder
                        ? m_codecDecoder->toUnicode(data.constData(),
                                                    int(data.size()))
                        : m_decoder.decode(data);
                m_term->recvData(text.toUtf8());
            });
    connect(m_transport, &ZzTransportInterface::stateChanged, this,
            [this](ZzTransportInterface::State state) {
                if (state == ZzTransportInterface::State::Connected) {
                    hideErrorBanner();
                }
                emit stateChanged(state);
            });
    connect(m_transport, &ZzTransportInterface::errorOccurred, this,
            [this](int, const QString &message) {
                showErrorBanner(message);
                emit errorOccurred(message);
            });
    connect(m_transport, &ZzTransportInterface::disconnected, this,
            [this](const QString &reason) { emit disconnected(reason); });
    connect(m_transport, &ZzTransportInterface::tunnelCountChanged, this,
            &ZzTerminalView::tunnelCountChanged);
    connect(m_transport, &ZzTransportInterface::statusNotice, this,
            &ZzTerminalView::statusNotice);
}

ZzTransportInterface *ZzTerminalView::transport() const
{
    return m_transport;
}

QWidget *ZzTerminalView::errorBanner() const { return m_errorBanner; }
QLabel *ZzTerminalView::errorLabel() const { return m_errorLabel; }
QPushButton *ZzTerminalView::retryButton() const { return m_retryButton; }

void ZzTerminalView::retranslateUi()
{
    m_retryButton->setText(tr("重试"));
}

void ZzTerminalView::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(event);
}

void ZzTerminalView::showErrorBanner(const QString &message)
{
    m_errorLabel->setText(message);
    m_errorBanner->show();
}

void ZzTerminalView::hideErrorBanner()
{
    m_errorBanner->hide();
}

ZzCoreTerminalWidget *ZzTerminalView::termWidget() const
{
    return m_term;
}

ZzScrollbackBridge *ZzTerminalView::scrollbackBridge() const
{
    return m_scrollbackBridge;
}

void ZzTerminalView::openEndpoint(const ZzTransportEndpoint &endpoint)
{
    m_lastEndpoint = endpoint;
    if (m_transport) {
        m_transport->open(endpoint);
    }
}

QString ZzTerminalView::encoding() const
{
    return m_encoding;
}

ZzTransportInterface::State ZzTerminalView::transportState() const
{
    return m_transport ? m_transport->state()
                       : ZzTransportInterface::State::Disconnected;
}

void ZzTerminalView::applySettings(const ZzAppSettings &settings)
{
    m_term->setTerminalFontPointSize(settings.fontSize());

    m_encoding = settings.encoding();
    const QByteArray codecName = settings.encoding().toUtf8();
    // 两级查找：① 按名直接构造（UTF 系/Latin1 由 QtCore 内建，GBK 等经
    // QtCore iconv 或 ICU 按名命中——encodingForName 只认内建枚举，看不到
    // 这些 codec，故不用它判有效）；② 按名不命中的平台（macOS 官方包无
    // iconv/ICU，实测 GBK 落空回退 UTF-8）用 Core5Compat 自带 codec 兜底，
    // 三平台口径统一。
    QStringDecoder decoder(codecName.constData());
    QStringEncoder encoder(codecName.constData());
    if (decoder.isValid() && encoder.isValid()) {
        m_decoder = std::move(decoder);
        m_encoder = std::move(encoder);
        m_codecDecoder.reset();
        m_codecEncoder.reset();
    } else if (QTextCodec *codec = QTextCodec::codecForName(codecName)) {
        // 有状态转码器：多字节序列可跨 TCP 分片，与 QStringDecoder 同语义
        m_codecDecoder.reset(codec->makeDecoder());
        m_codecEncoder.reset(codec->makeEncoder());
    } else {
        // 无效编码名（拼写错误等）回退 UTF-8 保底：保持默认态 decoder 会让
        // decode 静默返空串导致黑屏。
        qWarning() << "ZzTerminalView: 无法识别的编码" << settings.encoding()
                   << "，收发转码回退 UTF-8";
        m_decoder = QStringDecoder(QStringConverter::Utf8);
        m_encoder = QStringEncoder(QStringConverter::Utf8);
        m_codecDecoder.reset();
        m_codecEncoder.reset();
    }
    m_term->setColorScheme(settings.colorScheme());
    // historyLines 运行时调整仍不生效（Core 构造期容量），保留读存。
    (void)settings.historyLines();
}

void ZzTerminalView::enableScrollback(const QString &sessionId)
{
    if (m_scrollbackBridge) {
        return; // 每会话只建一次
    }
    // 温层文件落在应用配置目录下按会话 ID 区分（QUuid 字符串，文件名安全）。
    // 同一 profile 可同时开多个标签（sessionId 相同），必须追加每标签唯一后缀，
    // 否则后开标签与先开标签共用同一温层文件路径，互相干扰
    static std::atomic<int> s_warmSequence = 0;
    const QString dirPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/scrollback");
    QDir().mkpath(dirPath);
    const QString warmPath = dirPath + QLatin1Char('/') + sessionId
                             + QLatin1Char('-')
                             + QString::number(s_warmSequence.fetch_add(1))
                             + QStringLiteral(".warm");
    // 温层残留文件不再由本层预删：冷层模式下引擎 open() 会按文件头游标完成
    // 崩溃恢复续传后删除残留并创建全新空温层；冷层不可用时引擎降级为温层模式
    // 并自行删除残留全新开始（与 v0.1 行为一致，新会话显示层行号仍从 0 起）
    ZzLogEngine::Config config;
    config.warmFilePath = warmPath;
    config.coldDbPath = dirPath + QStringLiteral("/cold.db"); // 全局单库：所有会话共享
    config.sessionId = sessionId;                             // 会话 profile id（冷层行归属）
    auto *engine = new ZzLogEngine(config, this);
    m_scrollbackBridge = new ZzScrollbackBridge(m_term, engine, this);
    // 降级 → 状态栏提示（经 errorOccurred 同一路径到 ZzTabManager::statusMessage）
    connect(m_scrollbackBridge, &ZzScrollbackBridge::degraded, this,
            [this](const QString &reason) {
                emit errorOccurred(tr("滚动历史已降级为内存模式：%1")
                                       .arg(reason));
            });
    // 冷层降级 → 状态栏提示（与温层降级同一路径，不打断终端）
    connect(engine, &ZzLogEngine::degradedToWarmOnly, this,
            [this](const QString &reason) {
                emit errorOccurred(tr("滚动历史已降级为温层模式：%1")
                                       .arg(reason));
            });
    // open 必须最后调用：温层打开失败时它会同步发射 degradedToMemoryOnly，
    // 先于桥与提示链路接线调用会丢失该降级提示（规格 §八）
    engine->open();
}
