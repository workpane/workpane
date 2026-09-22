#include "ui/TabBar.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QPainter>

#include <algorithm>

namespace workpane::ui {

constexpr int closeButtonSize = 15;
constexpr int tabHorizontalMargin = 12;
constexpr int tabIndicatorSize = 7;
constexpr int tabIndicatorSpacing = 8;
constexpr int closeButtonReserve = closeButtonSize + 10;

TabCloseButton::TabCloseButton(const Theme& theme, QWidget* parent) : QAbstractButton(parent), m_theme(&theme) {
    setObjectName(QStringLiteral("tabCloseButton"));
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(sizeHint());
}

void TabCloseButton::applyTheme(const Theme& theme) {
    m_theme = &theme;
    update();
}

QSize TabCloseButton::sizeHint() const {
    return {closeButtonSize, closeButtonSize};
}

void TabCloseButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor circle = m_theme->color(ThemeColor::Danger);
    circle.setAlphaF(m_hovered || isDown() ? 1.0F : 0.72F);
    painter.setPen(Qt::NoPen);
    painter.setBrush(circle);
    painter.drawEllipse(rect());

    QPen cross(m_theme->color(ThemeColor::OnAccent));
    cross.setWidthF(1.4);
    cross.setCapStyle(Qt::RoundCap);
    painter.setPen(cross);
    const qreal inset = width() * 0.32;
    painter.drawLine(QPointF(inset, inset), QPointF(width() - inset, height() - inset));
    painter.drawLine(QPointF(width() - inset, inset), QPointF(inset, height() - inset));
}

void TabCloseButton::enterEvent(QEnterEvent* event) {
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void TabCloseButton::leaveEvent(QEvent* event) {
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

TabBar::TabBar(const Theme& theme, QWidget* parent) : QTabBar(parent), m_theme(&theme) {
    setObjectName(QStringLiteral("appTabBar"));
    setDrawBase(false);
    setExpanding(false);
    setElideMode(Qt::ElideRight);
    setUsesScrollButtons(true);
    setFocusPolicy(Qt::NoFocus);
}

void TabBar::applyTheme(const Theme& theme) {
    m_theme = &theme;

    for (int index = 0; index < count(); ++index) {
        if (auto* button = qobject_cast<TabCloseButton*>(tabButton(index, QTabBar::RightSide)); button != nullptr) {
            button->applyTheme(theme);
        }
    }

    update();
}

void TabBar::tabInserted(int index) {
    QTabBar::tabInserted(index);
    installCloseButton(index);
}

// Qt installs its own close button before reporting the insertion, so the tab bar replaces it with the application button.
void TabBar::installCloseButton(int index) {
    if (!tabsClosable()) {
        return;
    }
    if (auto* existing = tabButton(index, QTabBar::RightSide); qobject_cast<TabCloseButton*>(existing) != nullptr) {
        return;
    }

    auto* button = new TabCloseButton(*m_theme, this);
    // clang-format off
    connect(button, &QAbstractButton::clicked, this, [this, button]() {
        for (int candidate = 0; candidate < count(); ++candidate) {
            if (tabButton(candidate, QTabBar::RightSide) == button) {
                emit tabCloseRequested(candidate);
                return;
            }
        }
    });
    // clang-format on
    setTabButton(index, QTabBar::RightSide, button);
}

QSize TabBar::tabSizeHint(int index) const {
    const QFontMetrics metrics(font());
    const int iconWidth = tabIcon(index).isNull() ? 0 : m_theme->metric(ThemeMetric::SmallIconSize) + 6;
    const int closeWidth = tabsClosable() ? closeButtonReserve : 0;
    const int width = tabHorizontalMargin * 2 + tabIndicatorSize + tabIndicatorSpacing + iconWidth + metrics.horizontalAdvance(tabText(index)) + closeWidth;
    return {std::min(width, 260), m_theme->metric(ThemeMetric::WorkspaceBarHeight)};
}

// The selected tab is marked by the shared accent square instead of a bar, and the divider under the strip belongs to the container.
void TabBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), m_theme->color(ThemeColor::Panel));

    for (int index = 0; index < count(); ++index) {
        const QRect bounds = tabRect(index);
        const bool selected = index == currentIndex();
        painter.fillRect(bounds, selected ? m_theme->color(ThemeColor::Window) : m_theme->color(ThemeColor::Panel));

        painter.setPen(m_theme->color(ThemeColor::Border));
        painter.drawLine(bounds.topRight(), bounds.bottomRight());

        QRect content = bounds.adjusted(tabHorizontalMargin, 0, -tabHorizontalMargin, 0);
        if (tabsClosable()) {
            content.setRight(content.right() - closeButtonReserve);
        }

        const QRect indicator(content.left(), content.center().y() - tabIndicatorSize / 2, tabIndicatorSize, tabIndicatorSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_theme->color(selected ? ThemeColor::Accent : ThemeColor::TextMuted));
        painter.drawEllipse(indicator);
        content.setLeft(indicator.right() + tabIndicatorSpacing);

        if (!tabIcon(index).isNull()) {
            const int iconSize = m_theme->metric(ThemeMetric::SmallIconSize);
            const QRect iconBounds(content.left(), content.center().y() - iconSize / 2 + 1, iconSize, iconSize);
            tabIcon(index).paint(&painter, iconBounds, Qt::AlignCenter, selected ? QIcon::Normal : QIcon::Disabled);
            content.setLeft(iconBounds.right() + 6);
        }

        painter.setPen(m_theme->color(selected ? ThemeColor::Text : ThemeColor::TextMuted));
        painter.setBrush(Qt::NoBrush);
        painter.drawText(content, Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(tabText(index), Qt::ElideRight, content.width()));
    }
}

TabWidget::TabWidget(const Theme& theme, QWidget* parent) : QTabWidget(parent), m_bar(new TabBar(theme, this)) {
    setObjectName(QStringLiteral("appTabWidget"));
    setTabBar(m_bar);
    setDocumentMode(false);
    m_bar->setDrawBase(false);
    applyTheme(theme);
}

// The divider belongs to the pane border so the content starts below it, because a page paints over anything drawn on that row.
void TabWidget::applyTheme(const Theme& theme) {
    setStyleSheet(ThemeTokens::substituted(QStringLiteral("QTabWidget::pane { border-width: 1px 0 0 0; border-style: solid; border-color: @border; border-radius: 0; background: transparent; }"), theme));
    m_bar->applyTheme(theme);
    update();
}

} // namespace workpane::ui
