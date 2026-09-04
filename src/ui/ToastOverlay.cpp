#include "ui/ToastOverlay.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QRegion>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace workpane::ui {

constexpr int toastWidth = 380;
constexpr int toastSpacing = 10;
constexpr int toastMargin = 18;
constexpr int toastLifetimeMs = 6000;
constexpr int toastAnimationMs = 180;
constexpr qsizetype maximumVisibleToasts = 4;

class ToastOverlayHelper final {
  public:
    static IconName severityIcon(plugins::AlertSeverity severity);
    static ThemeColor severityColor(plugins::AlertSeverity severity);
};

IconName ToastOverlayHelper::severityIcon(plugins::AlertSeverity severity) {
    switch (severity) {
    case plugins::AlertSeverity::Information:
        return IconName::Information;
    case plugins::AlertSeverity::Success:
        return IconName::Success;
    case plugins::AlertSeverity::Warning:
        return IconName::Warning;
    case plugins::AlertSeverity::Error:
        return IconName::Error;
    }

    return IconName::Information;
}

ThemeColor ToastOverlayHelper::severityColor(plugins::AlertSeverity severity) {
    switch (severity) {
    case plugins::AlertSeverity::Information:
        return ThemeColor::Accent;
    case plugins::AlertSeverity::Success:
        return ThemeColor::Success;
    case plugins::AlertSeverity::Warning:
        return ThemeColor::Warning;
    case plugins::AlertSeverity::Error:
        return ThemeColor::Danger;
    }

    return ThemeColor::Accent;
}

class Toast final : public QFrame {
  public:
    Toast(const Theme& theme, const QString& title, const QString& message, plugins::AlertSeverity severity, std::function<void(Toast*)> onDismiss, QWidget* parent) : QFrame(parent), m_severity(severity), m_onDismiss(std::move(onDismiss)) {
        setObjectName(QStringLiteral("toast"));
        setFixedWidth(toastWidth);
        setAttribute(Qt::WA_StyledBackground, true);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(13, 11, 8, 11);
        layout->setSpacing(11);
        m_icon = new QLabel(this);
        m_icon->setFixedSize(18, 18);
        auto* text = new QVBoxLayout();
        text->setContentsMargins(0, 0, 0, 0);
        text->setSpacing(2);
        m_title = new QLabel(title, this);
        m_title->setObjectName(QStringLiteral("toastTitle"));
        m_title->setWordWrap(true);
        m_message = new QLabel(message, this);
        m_message->setObjectName(QStringLiteral("toastMessage"));
        m_message->setWordWrap(true);
        m_message->setVisible(!message.isEmpty());
        text->addWidget(m_title);
        text->addWidget(m_message);
        m_close = new QToolButton(this);
        m_close->setObjectName(QStringLiteral("toastClose"));
        m_close->setAutoRaise(true);
        layout->addWidget(m_icon, 0, Qt::AlignTop);
        layout->addLayout(text, 1);
        layout->addWidget(m_close, 0, Qt::AlignTop);

        m_opacity = new QGraphicsOpacityEffect(this);
        m_opacity->setOpacity(0.0);
        setGraphicsEffect(m_opacity);

        applyTheme(theme);

        m_lifetime.setSingleShot(true);
        m_lifetime.setInterval(toastLifetimeMs);
        connect(&m_lifetime, &QTimer::timeout, this, &Toast::dismiss);
        connect(m_close, &QToolButton::clicked, this, &Toast::dismiss);
    }

    void applyTheme(const Theme& theme) {
        const QColor accent = theme.color(ToastOverlayHelper::severityColor(m_severity));
        m_icon->setPixmap(IconCatalog::icon(ToastOverlayHelper::severityIcon(m_severity), accent).pixmap(18, 18));
        m_close->setIcon(IconCatalog::icon(IconName::Close, theme.color(ThemeColor::TextMuted)));
        m_close->setIconSize(QSize(12, 12));
        m_title->setFont(theme.font(ThemeFont::Interface));
        m_message->setFont(theme.font(ThemeFont::Interface));
        setStyleSheet(ThemeTokens::substituted(QStringLiteral("QFrame#toast { background: @raised; border: 1px solid @border; border-left: 3px solid %1; border-radius: 3px; } QLabel#toastTitle { color: @text; font-weight: 600; } QLabel#toastMessage { color: @textMuted; } QToolButton#toastClose { border: none; border-radius: @controlRadiuspx; padding: 2px; } QToolButton#toastClose:hover { background: @hover; }").arg(accent.name()), theme));
    }

    void appear() {
        show();
        raise();
        animate(1.0, false);
        m_lifetime.start();
    }

    [[nodiscard]] int preferredHeight(int availableWidth) const {
        return hasHeightForWidth() ? heightForWidth(availableWidth) : sizeHint().height();
    }

    void dismiss() {
        if (m_dismissing) {
            return;
        }

        m_dismissing = true;
        m_lifetime.stop();
        m_close->setEnabled(false);
        animate(0.0, true);
    }

    [[nodiscard]] bool isDismissing() const {
        return m_dismissing;
    }

  private:
    void animate(double target, bool notifyOnFinish) {
        auto* animation = new QPropertyAnimation(m_opacity, QByteArrayLiteral("opacity"), this);
        animation->setDuration(toastAnimationMs);
        animation->setStartValue(m_opacity->opacity());
        animation->setEndValue(target);
        animation->setEasingCurve(QEasingCurve::OutCubic);

        if (notifyOnFinish) {
            // clang-format off
            connect(animation, &QPropertyAnimation::finished, this, [this]() { m_onDismiss(this); });
            // clang-format on
        }

        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }

    plugins::AlertSeverity m_severity;
    std::function<void(Toast*)> m_onDismiss;
    QLabel* m_icon{nullptr};
    QLabel* m_title{nullptr};
    QLabel* m_message{nullptr};
    QToolButton* m_close{nullptr};
    QGraphicsOpacityEffect* m_opacity{nullptr};
    QTimer m_lifetime;
    bool m_dismissing{false};
};

ToastOverlay::ToastOverlay(const Theme& theme, QWidget* host) : QWidget(host), m_theme(&theme) {
    setObjectName(QStringLiteral("toastOverlay"));
    setAttribute(Qt::WA_NoSystemBackground);
    host->installEventFilter(this);
    relayout();
}

void ToastOverlay::showNotification(const QString& title, const QString& message, plugins::AlertSeverity severity) {
    qsizetype liveCount = 0;

    for (auto* visible : m_toasts) {
        liveCount += visible->isDismissing() ? 0 : 1;
    }

    for (auto* visible : m_toasts) {
        if (liveCount < maximumVisibleToasts) {
            break;
        }
        if (!visible->isDismissing()) {
            visible->dismiss();
            --liveCount;
        }
    }

    // clang-format off
    auto* toast = new Toast(*m_theme, title, message, severity, [this](Toast* dismissed) { removeToast(dismissed); }, this);
    // clang-format on
    m_toasts.append(toast);
    relayout();
    toast->appear();
    raise();
}

void ToastOverlay::applyTheme(const Theme& theme) {
    m_theme = &theme;

    for (auto* toast : m_toasts) {
        toast->applyTheme(theme);
    }

    relayout();
}

void ToastOverlay::dismissAll() {
    const auto visible = m_toasts;

    for (auto* toast : visible) {
        toast->dismiss();
    }
}

bool ToastOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        relayout();
    }

    return QWidget::eventFilter(watched, event);
}

void ToastOverlay::removeToast(Toast* toast) {
    m_toasts.removeOne(toast);
    toast->deleteLater();
    relayout();
}

void ToastOverlay::relayout() {
    // The overlay is only as large as its live toasts, so no other point of the window is ever covered by it.
    if (m_toasts.isEmpty()) {
        hide();
        return;
    }

    QList<int> heights;
    int total = 0;

    for (auto* toast : m_toasts) {
        heights.append(toast->preferredHeight(toastWidth));
        total += heights.last() + toastSpacing;
    }

    total -= toastSpacing;

    const QRect available = parentWidget()->rect();
    setGeometry(available.width() - toastWidth - toastMargin, available.height() - toastMargin - total, toastWidth, total);

    int bottom = total;
    QRegion interactive;

    for (qsizetype index = m_toasts.size() - 1; index >= 0; --index) {
        const QRect local(0, bottom - heights.at(index), toastWidth, heights.at(index));
        m_toasts.at(index)->setGeometry(local);
        interactive += local;
        bottom -= heights.at(index) + toastSpacing;
    }

    // The gaps between stacked toasts stay click-through because the mask carries only the toast rectangles.
    setMask(interactive);
    show();
    raise();
}

} // namespace workpane::ui
