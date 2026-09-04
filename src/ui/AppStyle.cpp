#include "ui/AppStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QStyleOptionTab>
#include <QStyleOptionToolButton>

#include <algorithm>

namespace workpane::ui {

QString ApplicationStyleSheet::applicationStyleSheet(const Theme& theme) {
    const QString rules = QStringLiteral(R"(
        QWidget#modeBar { background: @window; border: none; }
        QWidget#findBar { background: @panel; border-top: 1px solid @border; }
        QToolButton#findCase, QToolButton#findWholeWord { border: 1px solid @border; border-radius: 3px; padding: 1px 6px; color: @textMuted; }
        QToolButton#findCase:hover, QToolButton#findWholeWord:hover { background: @hover; }
        QToolButton#findCase:checked, QToolButton#findWholeWord:checked { background: @accent; color: @onAccent; border-color: @accent; }
        QToolButton#toolbarActionButton { background: transparent; border: none; border-radius: @controlRadiuspx; margin: 1px; padding: 0 6px; }
        QToolButton#toolbarActionButton:hover { background: @hover; }
        QToolButton#toolbarActionButton:pressed { background: @borderStrong; }
        QToolButton#toolbarIconButton { background: transparent; border: none; border-radius: @controlRadiuspx; margin: 2px; padding: 0; }
        QToolButton#toolbarIconButton:hover { background: @hover; }
        QToolButton#inlineActionButton { background: transparent; border: none; border-radius: @controlRadiuspx; padding: 3px 6px; }
        QToolButton#inlineActionButton:hover { background: @hover; }
        QTabBar { background: transparent; }
        QTabBar::tab { min-width: 118px; max-width: 220px; font-size: @interfaceFontSizept; }
        QLabel#mutedLabel, QLabel#emptyState { color: @textMuted; }
        QPushButton { min-width: 0; }
        QPushButton:hover { background: @hover; }
        QPushButton:pressed { background: @borderStrong; }
        QLineEdit, QComboBox, QAbstractSpinBox, QPushButton { min-height: @controlTextHeightpx; background: @raised; border: 1px solid transparent; border-radius: @controlRadiuspx; padding: @controlVerticalPaddingpx @controlHorizontalPaddingpx; } QComboBox, QAbstractSpinBox { selection-background-color: @accent; }
        QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { border-color: @accent; }
        QComboBox { padding-right: 28px; }
        QToolButton#chipButton { background: transparent; border: 1px solid @borderStrong; border-radius: @badgeRadiuspx; padding: 0 @badgeHorizontalPaddingpx; color: @text; font-size: @interfaceFontSizept; }
        QToolButton#chipButton:hover { background: @accentStrong; border-color: @accentStrong; color: @onAccent; }
        workpane--ui--CalendarPopup { background: @panel; border: 1px solid @border; border-radius: @controlRadiuspx; }
        QLabel#calendarTitle { color: @text; font-size: @interfaceFontSizept; font-weight: 600; }
        QLabel#calendarWeekDay { color: @textMuted; font-size: @captionFontSizept; }
        QToolButton#calendarDay { background: transparent; border: none; border-radius: @controlRadiuspx; color: @text; font-size: @interfaceFontSizept; }
        QToolButton#calendarDay:hover { background: @hover; }
        QToolButton#calendarDay[outside="true"] { color: @textMuted; }
        QToolButton#calendarDay[chosen="true"] { background: @accent; color: @onAccent; }
        QToolButton#stepperButton { background: @raised; border: none; border-radius: @controlRadiuspx; margin: 0; padding: 0; }
        QToolButton#stepperButton:hover { background: @hover; }
        QListWidget, QTableWidget, QTableView { background: @window; border: none; outline: none; alternate-background-color: @panel; }
        QTextBrowser, QPlainTextEdit { background: @window; border: none; }
        workpane--ui--TextField { background: @raised; border: 1px solid @borderStrong; border-radius: @controlRadiuspx; padding: 4px 7px; selection-background-color: @accent; }
        workpane--ui--TextField:focus { border-color: @accent; }
        QListWidget::item { padding: 7px 9px; border-radius: @controlRadiuspx; }
        QListWidget::item:hover { background: @hover; }
        QListWidget::item:selected { background: @accent; color: @onAccent; }
        QTableWidget::item, QTableView::item { padding: 0 7px; }
        QTableWidget::item:selected, QTableView::item:selected { background: @accent; color: @onAccent; }
        QLabel#filterCaption { color: @textMuted; font-size: @captionFontSizept; }
        QLineEdit#filterField { background: @raised; border: 1px solid @borderStrong; border-radius: @controlRadiuspx; padding: @controlVerticalPaddingpx @controlHorizontalPaddingpx; }
        QLineEdit#filterField:focus { border-color: @accent; }
        QHeaderView::section { background: @panel; color: @textMuted; border: none; border-bottom: 1px solid @border; padding: 7px; }
        QWidget#pageHeader { background: @panel; border-bottom: 1px solid @border; }
        QStatusBar { background: @panel; border-top: 1px solid @border; }
        QStatusBar::item { border: none; }
        QWidget#codeEditorStatusBar { background: @panel; border-top: 1px solid @border; }
        QToolButton, QPushButton { font-size: @interfaceFontSizept; }
        QLabel#pageTitle { font-size: @pageTitleFontSizept; font-weight: 600; }
        QLabel#settingsSectionTitle { font-size: @sectionTitleFontSizept; font-weight: 600; color: @accent; }
        QWidget#settingsSearchBand { background: @panel; border-bottom: 1px solid @border; }
        QWidget#sharedDivider { background: @border; }
        QListWidget#settingsCategories { background: @panel; border-right: 1px solid @border; padding: 8px; }
        QListWidget#settingsCategories::item:selected { background: @hover; color: @text; border-left: 2px solid @accent; }
        QPushButton#primaryButton, QPushButton[primary="true"] { background: @accent; color: @onAccent; }
        QPushButton#primaryButton:hover, QPushButton[primary="true"]:hover { background: @accentHover; }
        QPushButton#destructiveButton { background: @danger; color: @onDanger; }
        QPushButton#destructiveButton:hover { background: @dangerHover; }
        QScrollBar:vertical { background: transparent; width: @scrollBarExtentpx; margin: 1px; }
        QScrollBar:horizontal { background: transparent; height: @scrollBarExtentpx; margin: 1px; }
        QScrollBar::handle:vertical { background: @borderStrong; min-height: 28px; border-radius: 4px; }
        QScrollBar::handle:horizontal { background: @borderStrong; min-width: 28px; border-radius: 4px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    )");
    return ThemeTokens::substituted(QStringLiteral("QMainWindow, QWidget { color: @text; }") + rules, theme);
}

AppStyle::AppStyle(QStyle* baseStyle) : QProxyStyle(baseStyle) {}

QColor AppStyle::color(Color role) {
    return ThemeManager::instance().theme().color(role);
}

QPalette AppStyle::applicationPalette() {
    return ThemeManager::instance().theme().palette();
}

int AppStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const {
    switch (metric) {
    case PM_DefaultFrameWidth:
        return 1;
    case PM_ButtonMargin:
        return 0;
    case PM_SmallIconSize:
        return ThemeManager::instance().theme().metric(ThemeMetric::SmallIconSize);
    case PM_ToolBarIconSize:
        return ThemeManager::instance().theme().metric(ThemeMetric::SmallIconSize);
    case PM_ScrollBarExtent:
        return ThemeManager::instance().theme().metric(ThemeMetric::ScrollBarExtent);
    case PM_SplitterWidth:
        return ThemeManager::instance().theme().metric(ThemeMetric::SplitterWidth);
    case PM_TabBarTabHSpace:
        return ThemeManager::instance().theme().metric(ThemeMetric::TabHorizontalPadding);
    case PM_TabBarTabVSpace:
        return ThemeManager::instance().theme().metric(ThemeMetric::TabVerticalPadding);
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int AppStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const {
    if (hint == SH_ToolButtonStyle) {
        return Qt::ToolButtonIconOnly;
    }
    if (hint == SH_Menu_AllowActiveAndDisabled) {
        return true;
    }
    // A native combo popup paints its own scrolling bands, so the selectable list is a plain flat list instead.
    if (hint == SH_ComboBox_Popup) {
        return 0;
    }

    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

QSize AppStyle::sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget) const {
    if (type == CT_PushButton || type == CT_ToolButton) {
        return contentsSize;
    }

    return QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
}

void AppStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const {
    if (element == PE_FrameFocusRect) {
        return;
    }

    if (element == PE_PanelButtonTool) {
        QColor background = Qt::transparent;
        if (option->state.testFlag(State_Sunken)) {
            background = color(Color::Pressed);
        } else if (option->state.testFlag(State_MouseOver)) {
            background = color(Color::Hover);
        }

        if (background.alpha() > 0) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(background);
            painter->drawRoundedRect(option->rect.adjusted(1, 1, -1, -1), 3, 3);
            painter->restore();
        }
        return;
    }

    if (element == PE_IndicatorArrowDown) {
        QColor foreground = color(Color::TextMuted);
        if (option->state.testFlag(State_MouseOver)) {
            foreground = color(Color::Text);
        } else if (!option->state.testFlag(State_Enabled)) {
            foreground.setAlphaF(0.42F);
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPen pen(foreground, 1.4);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        const QPointF center = option->rect.center();
        painter->drawLine(QPointF(center.x() - 3.5, center.y() - 1.5), QPointF(center.x(), center.y() + 2.0));
        painter->drawLine(QPointF(center.x(), center.y() + 2.0), QPointF(center.x() + 3.5, center.y() - 1.5));
        painter->restore();
        return;
    }

    if (element == PE_IndicatorArrowUp) {
        drawChevron(option, painter);
        return;
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

// The indicator is painted from the theme text color so it always separates itself from the control behind it.
void AppStyle::drawChevron(const QStyleOption* option, QPainter* painter) {
    const QRectF bounds = QRectF(option->rect);
    const qreal width = std::min<qreal>(9.0, bounds.width());
    const qreal height = width / 2.0;
    const QPointF center = bounds.center();

    QPainterPath chevron;
    chevron.moveTo(center.x() - width / 2.0, center.y() + height / 2.0);
    chevron.lineTo(center.x(), center.y() - height / 2.0);
    chevron.lineTo(center.x() + width / 2.0, center.y() + height / 2.0);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(color(option->state.testFlag(State_Enabled) ? Color::Text : Color::TextMuted), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPath(chevron);
    painter->restore();
}

void AppStyle::drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const {
    if (element == CE_ToolButtonLabel && drawToolButtonLabel(option, painter, widget)) {
        return;
    }

    if (element != CE_TabBarTabShape) {
        QProxyStyle::drawControl(element, option, painter, widget);
        return;
    }

    const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);

    if (tab == nullptr) {
        return;
    }

    painter->save();
    const bool selected = tab->state.testFlag(State_Selected);
    const bool hovered = tab->state.testFlag(State_MouseOver);
    painter->fillRect(tab->rect, selected ? color(Color::Window) : hovered ? color(Color::Hover) : color(Color::Panel));
    painter->restore();
}

bool AppStyle::drawToolButtonLabel(const QStyleOption* option, QPainter* painter, const QWidget* widget) const {
    const auto* toolButton = qstyleoption_cast<const QStyleOptionToolButton*>(option);

    if (toolButton == nullptr || toolButton->toolButtonStyle != Qt::ToolButtonTextBesideIcon || toolButton->icon.isNull() || toolButton->text.isEmpty()) {
        return false;
    }

    constexpr int contentSpacing = 5;
    const int textWidth = toolButton->fontMetrics.horizontalAdvance(toolButton->text);
    const int contentWidth = toolButton->iconSize.width() + contentSpacing + textWidth;
    QRect contentRect = toolButton->rect;

    if (toolButton->state.testFlag(State_Sunken)) {
        contentRect.translate(pixelMetric(PM_ButtonShiftHorizontal, option, widget), pixelMetric(PM_ButtonShiftVertical, option, widget));
    }

    const int contentLeft = contentRect.left() + (contentRect.width() - contentWidth) / 2;
    const QRect logicalIconRect(contentLeft, contentRect.center().y() - toolButton->iconSize.height() / 2, toolButton->iconSize.width(), toolButton->iconSize.height());
    const QRect logicalTextRect(contentLeft + toolButton->iconSize.width() + contentSpacing, contentRect.top(), textWidth, contentRect.height());
    const QRect iconRect = visualRect(toolButton->direction, contentRect, logicalIconRect);
    const QRect textRect = visualRect(toolButton->direction, contentRect, logicalTextRect);
    const QIcon::Mode iconMode = !toolButton->state.testFlag(State_Enabled) ? QIcon::Disabled : toolButton->state.testFlag(State_MouseOver) ? QIcon::Active : QIcon::Normal;
    const QIcon::State iconState = toolButton->state.testFlag(State_On) ? QIcon::On : QIcon::Off;
    toolButton->icon.paint(painter, iconRect, Qt::AlignCenter, iconMode, iconState);

    int textFlags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine | Qt::TextShowMnemonic;

    if (!styleHint(SH_UnderlineShortcut, option, widget)) {
        textFlags |= Qt::TextHideMnemonic;
    }

    drawItemText(painter, textRect, textFlags, toolButton->palette, toolButton->state.testFlag(State_Enabled), toolButton->text, QPalette::ButtonText);
    return true;
}

} // namespace workpane::ui
