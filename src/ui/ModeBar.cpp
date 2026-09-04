#include "ui/ModeBar.h"

#include "ui/AppStyle.h"
#include "ui/Components.h"
#include "ui/Theme.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace workpane::ui {

class ModeButton final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal fader READ fader WRITE setFader)

  public:
    ModeButton(QString modeId, QIcon icon, QString title, QWidget* parent = nullptr) : QWidget(parent), m_modeId(std::move(modeId)), m_icon(std::move(icon)), m_title(std::move(title)) {
        setObjectName(m_modeId);
        setMinimumWidth(ThemeManager::instance().theme().metric(ThemeMetric::ModeBarMinimumWidth));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(m_title);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

    [[nodiscard]] const QString& modeId() const {
        return m_modeId;
    }

    [[nodiscard]] qreal fader() const {
        return m_fader;
    }

    [[nodiscard]] int minimumReadableWidth() const {
        const int required = Components::longestWordWidth(m_title, QFontMetrics(readableLabelFont())) + (ThemeManager::instance().theme().metric(ThemeMetric::ModeButtonHorizontalPadding) * 2);
        return std::clamp(required, ThemeManager::instance().theme().metric(ThemeMetric::ModeBarMinimumWidth), ThemeManager::instance().theme().metric(ThemeMetric::ModeBarMaximumWidth));
    }

    void setContentWidth(int width) {
        const int horizontalPadding = ThemeManager::instance().theme().metric(ThemeMetric::ModeButtonHorizontalPadding);
        const int textWidth = width - (horizontalPadding * 2);
        const QFontMetrics fontMetrics(readableLabelFont());
        const QRect textBounds = fontMetrics.boundingRect(QRect(0, 0, textWidth, QWIDGETSIZE_MAX), Qt::AlignHCenter | wrapping(textWidth), m_title);
        const int contentHeight = ThemeManager::instance().theme().metric(ThemeMetric::ModeButtonLabelTop) + textBounds.height() + ThemeManager::instance().theme().metric(ThemeMetric::ModeButtonBottomPadding);

        setFixedHeight(std::max(ThemeManager::instance().theme().metric(ThemeMetric::ModeButtonMinimumHeight), contentHeight));
    }

    void setFader(qreal value) {
        m_fader = value;
        update();
    }

    void setSelected(bool selected) {
        if (m_selected == selected) {
            return;
        }

        m_selected = selected;
        update();
    }

  signals:
    void activated(const QString& modeId);

  protected:
    void enterEvent(QEnterEvent* event) override {
        QWidget::enterEvent(event);
        animateTo(1.0);
    }

    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        animateTo(0.0);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit activated(m_modeId);
            event->accept();
            return;
        }

        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);

        if (m_selected) {
            painter.fillRect(rect(), AppStyle::color(AppStyle::Color::Raised));
        } else if (m_fader > 0.0) {
            QColor hover = AppStyle::color(AppStyle::Color::Hover);
            hover.setAlphaF(static_cast<float>(m_fader * 0.72));
            painter.fillRect(rect(), hover);
        }

        if (m_selected) {
            painter.fillRect(QRect(0, 0, 2, height()), AppStyle::color(AppStyle::Color::Accent));
        }

        const QIcon::Mode mode = m_selected ? QIcon::Active : QIcon::Normal;
        const int iconSize = 19;
        const int iconOffset = (width() - iconSize) / 2;
        m_icon.paint(&painter, QRect(iconOffset, 8, iconSize, iconSize), Qt::AlignCenter, mode);

        QFont labelFont = readableLabelFont();
        labelFont.setWeight(m_selected ? QFont::DemiBold : QFont::Normal);
        painter.setFont(labelFont);
        painter.setPen(m_selected ? AppStyle::color(AppStyle::Color::Text) : AppStyle::color(AppStyle::Color::TextMuted));
        const auto& theme = ThemeManager::instance().theme();
        const int horizontalPadding = theme.metric(ThemeMetric::ModeButtonHorizontalPadding);
        const int labelTop = theme.metric(ThemeMetric::ModeButtonLabelTop);
        const int textWidth = width() - (horizontalPadding * 2);
        painter.drawText(QRect(horizontalPadding, labelTop, textWidth, height() - labelTop - theme.metric(ThemeMetric::ModeButtonBottomPadding)), Qt::AlignHCenter | Qt::AlignTop | wrapping(textWidth), m_title);
    }

  private:
    [[nodiscard]] QFont readableLabelFont() const {
        return ThemeManager::instance().theme().font(ThemeFont::Navigation);
    }

    [[nodiscard]] Qt::TextFlag wrapping(int textWidth) const {
        return Components::labelWrapping(m_title, QFontMetrics(readableLabelFont()), textWidth);
    }

    void animateTo(qreal value) {
        auto* animation = new QPropertyAnimation(this, "fader", this);
        animation->setDuration(value > m_fader ? 80 : 160);
        animation->setStartValue(m_fader);
        animation->setEndValue(value);
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }

    QString m_modeId;
    QIcon m_icon;
    QString m_title;
    qreal m_fader{0.0};
    bool m_selected{false};
};

ModeBar::ModeBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("modeBar"));
    m_preferredWidth = ThemeManager::instance().theme().metric(ThemeMetric::ModeBarMinimumWidth);
    setMinimumWidth(ThemeManager::instance().theme().metric(ThemeMetric::ModeBarMinimumWidth));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* top = new QWidget(this);
    m_topLayout = new QVBoxLayout(top);
    m_topLayout->setContentsMargins(0, 0, 0, 0);
    m_topLayout->setSpacing(0);
    auto* bottom = new QWidget(this);
    m_bottomLayout = new QVBoxLayout(bottom);
    m_bottomLayout->setContentsMargins(0, 0, 0, 0);
    m_bottomLayout->setSpacing(0);
    root->addWidget(top);
    root->addStretch(1);
    root->addWidget(bottom);
}

QSize ModeBar::sizeHint() const {
    QSize preferred = QWidget::sizeHint();
    preferred.setWidth(m_preferredWidth);
    return preferred;
}

void ModeBar::addMode(const QString& modeId, const QIcon& modeIcon, const QString& title, plugins::NavigationPlacement placement) {
    auto* button = new ModeButton(modeId, modeIcon, title, this);
    m_buttons.append(button);
    m_preferredWidth = std::max(m_preferredWidth, button->minimumReadableWidth());
    updateButtonGeometry();

    auto* layout = placement == plugins::NavigationPlacement::Secondary ? m_bottomLayout : m_topLayout;
    layout->addWidget(button);
    connect(button, &ModeButton::activated, this, &ModeBar::activateMode);
}

void ModeBar::setCurrentMode(const QString& modeId) {
    for (auto* button : m_buttons) {
        button->setSelected(button->modeId() == modeId);
    }
}

void ModeBar::activateMode(const QString& modeId) {
    emit modeRequested(modeId);
}

void ModeBar::updateButtonGeometry() {
    for (auto* button : m_buttons) {
        button->setContentWidth(m_preferredWidth);
    }

    updateGeometry();
}

} // namespace workpane::ui

#include "ModeBar.moc"
