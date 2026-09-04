#include "ui/Components.h"

#include "ui/Theme.h"

#include <QAbstractItemModel>
#include <QDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace workpane::ui {

// What a cell draws and in which role, so the grid can repaint it while its row is selected.
// The spinner turns once a second at the rhythm of the screen, which is what makes it read as motion rather than as a jump.
constexpr int calendarMargin = 8;
constexpr int calendarSpacing = 2;
constexpr int calendarCellSize = 34;
constexpr int calendarWeeks = 6;
constexpr int daysInWeek = 7;
constexpr int busyFrameMs = 16;
constexpr int busyTurnMs = 1000;
constexpr int busySweepDegrees = 110;
constexpr int iconNameRole = Qt::UserRole + 32;
constexpr int iconRoleRole = Qt::UserRole + 33;

PageHeader::PageHeader(const Theme& theme, const QString& title, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("pageHeader"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(theme.metric(ThemeMetric::PageHeaderHeight));

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(14, 0, 8, 0);
    m_layout->setSpacing(8);

    m_title = new QLabel(title, this);
    m_title->setObjectName(QStringLiteral("pageTitle"));
    m_layout->addWidget(m_title);
}

ComboBox::ComboBox(const Theme& theme, QWidget* parent) : QComboBox(parent) {
    applyTheme(theme);
}

void ComboBox::applyTheme(const Theme& theme) {
    m_theme = &theme;
    // The popup belongs to this component, so its rules travel with it instead of depending on an ancestor style sheet.
    setStyleSheet(ThemeTokens::substituted(QStringLiteral("QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: @comboIndicatorWidthpx; border: none; background: transparent; } QComboBox::down-arrow { image: none; width: 0px; height: 0px; } QComboBox QAbstractItemView { background: @panel; border: 1px solid @border; border-radius: @controlRadiuspx; padding: 4px; outline: none; } QComboBox QAbstractItemView::item { min-height: 24px; padding: 4px 8px; border-radius: @controlRadiuspx; color: @text; } QComboBox QAbstractItemView::item:hover { background: @hover; } QComboBox QAbstractItemView::item:selected { background: @accent; color: @onAccent; }"), theme));
    update();
}

void ComboBox::paintEvent(QPaintEvent* event) {
    QComboBox::paintEvent(event);

    const qreal width = 9.0;
    const qreal height = width / 2.0;
    const QPointF center(rect().right() - m_theme->metric(ThemeMetric::ComboIndicatorWidth) / 2.0, rect().center().y());
    QPainterPath chevron;
    chevron.moveTo(center.x() - width / 2.0, center.y() - height / 2.0);
    chevron.lineTo(center.x(), center.y() + height / 2.0);
    chevron.lineTo(center.x() + width / 2.0, center.y() - height / 2.0);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(m_theme->color(isEnabled() ? ThemeColor::Text : ThemeColor::TextMuted), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(chevron);
}

CalendarPopup::CalendarPopup(const Theme& theme, QWidget* parent) : QWidget(parent, Qt::Popup), m_theme(theme) {
    setObjectName(QStringLiteral("calendarPopup"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(calendarMargin, calendarMargin, calendarMargin, calendarMargin);
    root->setSpacing(calendarSpacing);

    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(calendarSpacing);
    auto* previous = Components::toolButton(IconName::Back, theme, {}, header);
    previous->setObjectName(QStringLiteral("calendarPrevious"));
    auto* next = Components::toolButton(IconName::Forward, theme, {}, header);
    next->setObjectName(QStringLiteral("calendarNext"));
    m_title = new QLabel(header);
    m_title->setObjectName(QStringLiteral("calendarTitle"));
    m_title->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(previous);
    headerLayout->addWidget(m_title, 1);
    headerLayout->addWidget(next);
    root->addWidget(header);

    auto* days = new QWidget(this);
    m_grid = new QGridLayout(days);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(calendarSpacing);
    root->addWidget(days);

    const QLocale locale = QLocale::system();

    for (int column = 0; column < daysInWeek; ++column) {
        auto* name = new QLabel(locale.dayName(column + 1, QLocale::ShortFormat), days);
        name->setObjectName(QStringLiteral("calendarWeekDay"));
        name->setAlignment(Qt::AlignCenter);
        m_grid->addWidget(name, 0, column);
    }

    for (int index = 0; index < daysInWeek * calendarWeeks; ++index) {
        auto* cell = new QToolButton(days);
        cell->setObjectName(QStringLiteral("calendarDay"));
        cell->setAutoRaise(true);
        cell->setFixedSize(calendarCellSize, calendarCellSize);
        // clang-format off
        connect(cell, &QToolButton::clicked, this, [this, cell]() { emit dateChosen(cell->property("date").toDate()); hide(); });
        // clang-format on
        m_grid->addWidget(cell, 1 + index / daysInWeek, index % daysInWeek);
        m_cells.append(cell);
    }

    // clang-format off
    connect(previous, &QToolButton::clicked, this, [this]() { step(-1); });
    connect(next, &QToolButton::clicked, this, [this]() { step(1); });
    // clang-format on
}

void CalendarPopup::showFor(const QDate& date, QWidget* anchor) {
    m_selected = date;
    m_month = QDate(date.year(), date.month(), 1);
    rebuild();
    adjustSize();
    move(anchor->mapToGlobal(QPoint(0, anchor->height())));
    show();
}

void CalendarPopup::step(int months) {
    m_month = m_month.addMonths(months);
    rebuild();
}

// Every cell says which day it carries and whether it belongs to the month being read, so nothing is elided and nothing is guessed.
void CalendarPopup::rebuild() {
    const QLocale locale = QLocale::system();
    m_title->setText(QStringLiteral("%1 %2").arg(locale.monthName(m_month.month()), QString::number(m_month.year())));

    const int leading = m_month.dayOfWeek() - 1;
    const QDate first = m_month.addDays(-leading);

    for (int index = 0; index < m_cells.size(); ++index) {
        const QDate date = first.addDays(index);
        QToolButton* cell = m_cells.at(index);
        cell->setText(QString::number(date.day()));
        cell->setProperty("date", date);
        cell->setProperty("outside", date.month() != m_month.month());
        cell->setProperty("chosen", date == m_selected);
        cell->style()->unpolish(cell);
        cell->style()->polish(cell);
    }
}

DateTimeField::DateTimeField(const Theme& theme, QWidget* parent) : QDateTimeEdit(parent), m_theme(theme) {
    setCalendarPopup(false);
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    lineEdit()->setTextMargins(0, 0, theme.metric(ThemeMetric::ComboIndicatorWidth), 0);
    lineEdit()->installEventFilter(this);
}

bool DateTimeField::eventFilter(QObject* watched, QEvent* event) {
    if (watched == lineEdit() && event->type() == QEvent::MouseButtonPress) {
        const QPoint position = lineEdit()->mapTo(this, static_cast<QMouseEvent*>(event)->pos());
        if (indicatorRect().contains(position)) {
            openCalendar();
            return true;
        }
    }

    return QDateTimeEdit::eventFilter(watched, event);
}

QRect DateTimeField::indicatorRect() const {
    const int width = m_theme.metric(ThemeMetric::ComboIndicatorWidth);
    return {rect().right() - width, rect().top(), width, rect().height()};
}

void DateTimeField::paintEvent(QPaintEvent* event) {
    QDateTimeEdit::paintEvent(event);

    const qreal width = 9.0;
    const qreal height = width / 2.0;
    const QPointF center(indicatorRect().center().x(), rect().center().y());
    QPainterPath chevron;
    chevron.moveTo(center.x() - width / 2.0, center.y() - height / 2.0);
    chevron.lineTo(center.x(), center.y() + height / 2.0);
    chevron.lineTo(center.x() + width / 2.0, center.y() - height / 2.0);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(m_theme.color(isEnabled() ? ThemeColor::Text : ThemeColor::TextMuted), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(chevron);
}

void DateTimeField::mousePressEvent(QMouseEvent* event) {
    if (indicatorRect().contains(event->pos())) {
        openCalendar();
        return;
    }

    QDateTimeEdit::mousePressEvent(event);
}

void DateTimeField::openCalendar() {
    if (m_calendar == nullptr) {
        m_calendar = new CalendarPopup(m_theme, this);
        // clang-format off
        connect(m_calendar, &CalendarPopup::dateChosen, this, [this](const QDate& date) { setDateTime(QDateTime(date, time())); });
        // clang-format on
    }

    m_calendar->showFor(date(), this);
}

SecretField::SecretField(const Theme& theme, const QString& placeholder, RevealConfirmation confirmReveal, QWidget* parent) : QWidget(parent), m_confirmReveal(std::move(confirmReveal)) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_editor = new QLineEdit(this);
    m_editor->setEchoMode(QLineEdit::Password);
    m_editor->setPlaceholderText(placeholder);
    m_theme = &theme;
    m_reveal = Components::toolButton(IconName::Visible, theme, placeholder, this);
    m_reveal->setCheckable(true);
    layout->addWidget(m_editor, 1);
    layout->addWidget(m_reveal);

    connect(m_editor, &QLineEdit::editingFinished, this, &SecretField::editingFinished);
    connect(m_reveal, &QToolButton::clicked, this, &SecretField::toggleReveal);
}

QString SecretField::value() const {
    return m_editor->text();
}

void SecretField::setValue(const QString& value) {
    const QSignalBlocker blocker(m_editor);
    m_editor->setText(value);
}

bool SecretField::revealed() const {
    return m_editor->echoMode() == QLineEdit::Normal;
}

void SecretField::toggleReveal() {
    if (revealed()) {
        applyReveal(false);
        return;
    }

    applyReveal(m_confirmReveal && m_confirmReveal());
}

// The button states what the next click does, so a revealed secret offers to hide it again.
void SecretField::applyReveal(bool revealed) {
    m_reveal->setChecked(revealed);
    m_reveal->setIcon(IconCatalog::icon(revealed ? IconName::Hidden : IconName::Visible, *m_theme));
    m_editor->setEchoMode(revealed ? QLineEdit::Normal : QLineEdit::Password);
}

void PageHeader::setTitle(const QString& title) {
    m_title->setText(title);
}

void PageHeader::addWidget(QWidget* widget, int stretch) {
    m_layout->addWidget(widget, stretch);
}

void PageHeader::addStretch() {
    m_layout->addStretch(1);
}

void PageHeader::setSpacing(int spacing) {
    m_layout->setSpacing(spacing);
}

class ComponentsHelper final {
  public:
    static QString hardBreaks(const QString& text);
    static QString emojiFontFamily();
    static void resolveCodeFamily(QTextDocument* document, const QString& family);
    static int standardControlHeight(QWidget* parent);
};

// The height of a text control is what the platform and the active style make it, so it is measured rather than declared.
int ComponentsHelper::standardControlHeight(QWidget* parent) {
    QComboBox reference(parent);
    reference.setVisible(false);
    return reference.sizeHint().height();
}

// The Markdown reader names a family only on the runs it read as code, and which name that is differs by platform, so the run is found by the property rather than by its value.
void ComponentsHelper::resolveCodeFamily(QTextDocument* document, const QString& family) {
    QVector<QPair<int, int>> spans;

    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto entry = block.begin(); entry != block.end(); ++entry) {
            const QTextFragment fragment = entry.fragment();

            if (fragment.isValid() && fragment.charFormat().hasProperty(QTextFormat::FontFamilies)) {
                spans.append({fragment.position(), fragment.length()});
            }
        }
    }

    if (spans.isEmpty()) {
        return;
    }

    QTextCharFormat resolved;
    resolved.setFontFamilies({family});
    resolved.setFontFixedPitch(true);
    QTextCursor cursor(document);

    for (const auto& span : spans) {
        cursor.setPosition(span.first);
        cursor.setPosition(span.first + span.second, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(resolved);
    }
}

// A newline inside a fenced block is already a line of code, so only the prose between them is broken.
QString ComponentsHelper::hardBreaks(const QString& text) {
    const QStringList lines = text.split(QLatin1Char('\n'));
    QStringList written;
    bool fenced = false;

    for (const auto& line : lines) {
        const bool fence = line.trimmed().startsWith(QStringLiteral("```"));
        if (fence) {
            fenced = !fenced;
        }
        const bool breakable = !fenced && !fence && !line.trimmed().isEmpty();
        written.append(breakable ? line + QStringLiteral("  ") : line);
    }

    return written.join(QLatin1Char('\n'));
}

QString ComponentsHelper::emojiFontFamily() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("Apple Color Emoji");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Segoe UI Emoji");
#else
    return QStringLiteral("Noto Color Emoji");
#endif
}

MarkdownView::MarkdownView(const Theme& theme, QWidget* parent) : QTextBrowser(parent), m_theme(theme) {
    setFrameShape(QFrame::NoFrame);
    setOpenExternalLinks(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_fontSize = theme.font(ThemeFont::Interface).pointSize();
    applyTheme();
}

void MarkdownView::applyTheme() {
    QFont reading = m_theme.font(ThemeFont::Interface);
    reading.setPointSize(m_fontSize);
    document()->setDefaultFont(reading);
    document()->setDocumentMargin(0);
    // The emoji family is named so a glyph the reading family has no drawing for is still drawn.
    // Code is read as code, so it carries the monospace family over a surface of its own.
    document()->setDefaultStyleSheet(QStringLiteral("code, pre { font-family: '%1'; background-color: %2; } p, li, td, th, h1, h2, h3, h4, h5, h6 { font-family: '%3', '%4'; }").arg(m_theme.font(ThemeFont::Monospace).family(), m_theme.color(ThemeColor::Terminal).name(), reading.family(), ComponentsHelper::emojiFontFamily()));
}

void MarkdownView::setContentFontSize(int points) {
    m_fontSize = points;
    applyTheme();
}

void MarkdownView::setInk(const QColor& ink) {
    QPalette painted = palette();
    painted.setColor(QPalette::Text, ink);
    painted.setColor(QPalette::WindowText, ink);
    painted.setColor(QPalette::Base, Qt::transparent);
    setPalette(painted);
    setAutoFillBackground(false);
}

void MarkdownView::setDocumentMarkdown(const QString& text) {
    setMarkdown(text);
    ComponentsHelper::resolveCodeFamily(document(), m_theme.font(ThemeFont::Monospace).family());
}

void MarkdownView::setChatMarkdown(const QString& text) {
    setMarkdown(ComponentsHelper::hardBreaks(text));
    ComponentsHelper::resolveCodeFamily(document(), m_theme.font(ThemeFont::Monospace).family());
}

void MarkdownView::fitTo(int available) {
    if (available <= 0) {
        return;
    }

    QTextDocument* written = document();
    written->setTextWidth(available);
    const int wanted = std::clamp(static_cast<int>(std::ceil(written->idealWidth())), 1, available);
    written->setTextWidth(wanted);
    setFixedWidth(wanted);
    setFixedHeight(static_cast<int>(std::ceil(written->size().height())));
}

AvatarBadge::AvatarBadge(IconName name, const QColor& fill, const QColor& ink, int diameter, QWidget* parent) : QWidget(parent), m_icon(IconCatalog::icon(name, ink)), m_fill(fill), m_diameter(diameter) {
    setFixedSize(m_diameter, m_diameter);
}

void AvatarBadge::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_fill);
    painter.drawEllipse(rect());

    const int glyph = m_diameter * 3 / 5;
    const QRect target((m_diameter - glyph) / 2, (m_diameter - glyph) / 2, glyph, glyph);
    m_icon.paint(&painter, target);
}

RoundedSurface::RoundedSurface(QColor fill, int radius, QWidget* parent) : QWidget(parent), m_fill(fill), m_radius(radius) {
    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(4);
}

QVBoxLayout* RoundedSurface::content() const {
    return m_layout;
}

void RoundedSurface::paintEvent(QPaintEvent*) {
    QPainterPath shape;
    shape.addRoundedRect(QRectF(0, 0, width(), height()), m_radius, m_radius);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.fillPath(shape, m_fill);
}

BusyIndicator::BusyIndicator(const Theme& theme, QWidget* parent) : QWidget(parent), m_theme(theme) {
    const int size = theme.metric(ThemeMetric::SmallIconSize);
    setFixedSize(size, size);
    m_timer = new QTimer(this);
    m_timer->setInterval(busyFrameMs);
    // clang-format off
    connect(m_timer, &QTimer::timeout, this, [this]() { update(); });
    // clang-format on
    hide();
}

void BusyIndicator::setRunning(bool running) {
    setVisible(running);

    if (running) {
        m_clock.start();
        m_timer->start();
        return;
    }

    m_timer->stop();
}

// The angle is read from the clock rather than counted in frames, so a frame the interface was too busy to draw is skipped instead of stuttering.
void BusyIndicator::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen stroke(m_theme.color(ThemeColor::Accent));
    stroke.setWidthF(2.0);
    stroke.setCapStyle(Qt::RoundCap);
    painter.setPen(stroke);

    const qreal turn = static_cast<qreal>(m_clock.elapsed() % busyTurnMs) / busyTurnMs;
    const int start = static_cast<int>(turn * 360.0 * 16.0);
    const QRectF ring = QRectF(rect()).adjusted(2, 2, -2, -2);
    painter.drawArc(ring, -start, -busySweepDegrees * 16);
}

FilterField::FilterField(const QString& caption, const QString& placeholder, QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* label = new QLabel(caption, this);
    label->setObjectName(QStringLiteral("filterCaption"));
    m_editor = new QLineEdit(this);
    m_editor->setObjectName(QStringLiteral("filterField"));
    m_editor->setPlaceholderText(placeholder);
    m_editor->setClearButtonEnabled(true);
    layout->addWidget(label);
    layout->addWidget(m_editor);

    // clang-format off
    connect(m_editor, &QLineEdit::textChanged, this, [this](const QString& text) { emit filterChanged(text); });
    // clang-format on
}

QString FilterField::text() const {
    return m_editor->text();
}

void FilterField::clear() {
    m_editor->clear();
}

StatusIndicator::StatusIndicator(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("statusIndicator"));
    setFixedSize(8, 8);
}

void StatusIndicator::setColor(const QColor& color) {
    if (color == m_color) {
        return;
    }

    m_color = color;
    update();
}

void StatusIndicator::setSelectionInk(const QColor& ink) {
    if (ink == m_selectionInk) {
        return;
    }

    m_selectionInk = ink;
    update();
}

// The circle is painted instead of styled so the shape stays correct at any size.
void StatusIndicator::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_selectionInk.isValid() ? m_selectionInk : m_color);
    painter.drawEllipse(rect());
}

QTableWidgetItem* Components::gridStatusItem(const QString& text, IconName name, ThemeColor role, const Theme& theme) {
    auto* item = new QTableWidgetItem(text);
    item->setData(iconNameRole, static_cast<int>(name));
    item->setData(iconRoleRole, static_cast<int>(role));
    item->setIcon(IconCatalog::icon(name, theme.color(role)));
    item->setForeground(theme.color(role));
    return item;
}

void Components::setItemGlyph(QTreeWidgetItem* item, int column, IconName name, ThemeColor role, const Theme& theme) {
    item->setData(column, iconNameRole, static_cast<int>(name));
    item->setData(column, iconRoleRole, static_cast<int>(role));
    item->setIcon(column, IconCatalog::icon(name, theme.color(role)));
}

// A selected item is painted in the accent, so the glyph beside its name switches to the ink that reads on it.
void Components::repaintTreeGlyphs(QTreeWidget* tree, const Theme& theme) {
    QTreeWidgetItemIterator entry(tree);

    while (*entry != nullptr) {
        QTreeWidgetItem* item = *entry;
        for (int column = 0; column < tree->columnCount(); ++column) {
            if (!item->data(column, iconNameRole).isValid()) {
                continue;
            }
            const auto name = static_cast<IconName>(item->data(column, iconNameRole).toInt());
            const QColor ink = item->isSelected() ? theme.color(ThemeColor::OnAccent) : theme.color(static_cast<ThemeColor>(item->data(column, iconRoleRole).toInt()));
            item->setIcon(column, IconCatalog::icon(name, ink));
        }
        ++entry;
    }
}

QToolButton* Components::rowActionButton(IconName name, ThemeColor role, const Theme& theme, const QString& toolTip, QWidget* parent) {
    QToolButton* button = Components::toolButton(IconCatalog::icon(name, theme.color(role)), theme, toolTip, parent);
    button->setProperty("iconName", static_cast<int>(name));
    button->setProperty("iconRole", static_cast<int>(role));
    button->setAutoRaise(true);
    return button;
}

// An action inside the selected row is drawn on the accent, so it switches to the ink that reads on it.
void Components::repaintRowActions(QTableWidget* grid, const Theme& theme) {
    for (int row = 0; row < grid->rowCount(); ++row) {
        const bool selected = grid->selectionModel() != nullptr && grid->selectionModel()->isRowSelected(row);
        for (int column = 0; column < grid->columnCount(); ++column) {
            if (QTableWidgetItem* item = grid->item(row, column); item != nullptr && item->data(iconNameRole).isValid()) {
                const auto name = static_cast<IconName>(item->data(iconNameRole).toInt());
                const QColor ink = selected ? theme.color(ThemeColor::OnAccent) : theme.color(static_cast<ThemeColor>(item->data(iconRoleRole).toInt()));
                item->setIcon(IconCatalog::icon(name, ink));
                item->setForeground(ink);
            }
            QWidget* cell = grid->cellWidget(row, column);
            if (cell == nullptr) {
                continue;
            }
            for (auto* dot : cell->findChildren<StatusIndicator*>()) {
                dot->setSelectionInk(selected ? theme.color(ThemeColor::OnAccent) : QColor());
            }
            for (auto* action : cell->findChildren<QToolButton*>()) {
                const QVariant named = action->property("iconName");
                if (!named.isValid()) {
                    continue;
                }
                const IconName name = static_cast<IconName>(named.toInt());
                const QVariant role = action->property("iconRole");
                const QColor resting = role.isValid() ? theme.color(static_cast<ThemeColor>(role.toInt())) : theme.color(ThemeColor::Text);
                action->setIcon(IconCatalog::icon(name, selected ? theme.color(ThemeColor::OnAccent) : resting));
            }
        }
    }
}

QTableWidget* Components::dataGrid(const QStringList& headerLabels, QWidget* parent) {
    auto* grid = new QTableWidget(parent);
    grid->setColumnCount(static_cast<int>(headerLabels.size()));
    grid->setHorizontalHeaderLabels(headerLabels);
    grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid->setSelectionBehavior(QAbstractItemView::SelectRows);
    grid->setSelectionMode(QAbstractItemView::SingleSelection);
    grid->setAlternatingRowColors(true);
    grid->setShowGrid(false);
    grid->verticalHeader()->hide();
    grid->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    grid->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    grid->horizontalHeader()->setStretchLastSection(true);
    grid->setWordWrap(false);
    grid->setTextElideMode(Qt::ElideMiddle);
    const Theme& theme = ThemeManager::instance().theme();
    // A rebuilt row carries new actions and keeps the selection it had, so they are repainted once its cells are in place.
    auto* settled = new QTimer(grid);
    settled->setSingleShot(true);
    settled->setInterval(0);
    // clang-format off
    QObject::connect(settled, &QTimer::timeout, grid, [grid, &theme]() { Components::repaintRowActions(grid, theme); });
    QObject::connect(grid, &QTableWidget::itemSelectionChanged, grid, [settled]() { settled->start(); });
    QObject::connect(grid->model(), &QAbstractItemModel::dataChanged, grid, [settled]() { settled->start(); });
    QObject::connect(grid->model(), &QAbstractItemModel::rowsInserted, grid, [settled]() { settled->start(); });
    QObject::connect(grid->model(), &QAbstractItemModel::modelReset, grid, [settled]() { settled->start(); });
    QObject::connect(grid->model(), &QAbstractItemModel::layoutChanged, grid, [settled]() { settled->start(); });
    // clang-format on
    return grid;
}

// The stretched column absorbs the remaining width so every other column keeps its content width.
void Components::stretchGridColumn(QTableWidget* grid, int column) {
    grid->horizontalHeader()->setStretchLastSection(false);
    grid->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
}

// The glyph a row action draws is remembered, so a grid can repaint it in the ink its selection asks for.
QToolButton* Components::toolButton(const QIcon& buttonIcon, const Theme& theme, const QString& toolTip, QWidget* parent) {
    const int size = theme.metric(ThemeMetric::CompactButtonSize);
    const int iconSize = theme.metric(ThemeMetric::SmallIconSize);
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("toolbarIconButton"));
    button->setIcon(buttonIcon);
    button->setIconSize(QSize(iconSize, iconSize));
    button->setFixedSize(size, size);
    button->setToolTip(toolTip);
    return button;
}

QToolButton* Components::toolButton(IconName name, const Theme& theme, const QString& toolTip, QWidget* parent) {
    QToolButton* button = Components::toolButton(IconCatalog::icon(name, theme), theme, toolTip, parent);
    button->setProperty("iconName", static_cast<int>(name));
    return button;
}

// A row action that reads as words is a thin outlined chip, so it separates itself from the cell text without becoming a heavy button.
QToolButton* Components::chipButton(const QString& text, const Theme& theme, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("chipButton"));
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(theme.metric(ThemeMetric::BadgeRadius) * 2);
    return button;
}

// Every settings section starts with the same page shape so no owner invents its own margins or description style.
TextField::TextField(const QString& placeholder, QWidget* parent) : QPlainTextEdit(parent) {
    setPlaceholderText(placeholder);
}

SettingsSurface Components::settingsSectionPage(QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    return {page, layout};
}

// A selectable list is read by looking for one entry, so it is presented in alphabetical order whatever order it was built in.
void Components::sortComboBoxItems(QComboBox* box) {
    const QVariant selectedData = box->currentData();
    const QString selectedText = box->currentText();

    QVector<QPair<QString, QVariant>> items;
    items.reserve(box->count());

    for (int index = 0; index < box->count(); ++index) {
        items.append({box->itemText(index), box->itemData(index)});
    }
    // Case is folded before the comparison and only breaks a tie, because a locale-aware order puts every capital before every lowercase letter where the system declares no locale.
    // clang-format off
    const auto before = [](const QPair<QString, QVariant>& first, const QPair<QString, QVariant>& second) {
        const int folded = QString::compare(first.first, second.first, Qt::CaseInsensitive);
        return folded != 0 ? folded < 0 : QString::compare(first.first, second.first) < 0;
    };
    std::stable_sort(items.begin(), items.end(), before);
    // clang-format on

    const QSignalBlocker blocker(box);
    box->clear();

    for (const auto& item : items) {
        box->addItem(item.first, item.second);
    }

    if (box->isEditable()) {
        box->setCurrentText(selectedText);
        return;
    }

    box->setCurrentIndex(std::max(0, box->findData(selectedData)));
}

// A divider is a styled widget rather than a border, because a child laid out to the full width paints over the border of its container.
QWidget* Components::horizontalDivider(QWidget* parent) {
    auto* divider = new QWidget(parent);
    divider->setObjectName(QStringLiteral("sharedDivider"));
    divider->setAttribute(Qt::WA_StyledBackground, true);
    divider->setFixedHeight(1);
    return divider;
}

QWidget* Components::verticalDivider(QWidget* parent) {
    auto* divider = new QWidget(parent);
    divider->setObjectName(QStringLiteral("sharedDivider"));
    divider->setAttribute(Qt::WA_StyledBackground, true);
    divider->setFixedWidth(1);
    return divider;
}

QFormLayout* Components::settingsForm() {
    const int inset = ThemeManager::instance().theme().metric(ThemeMetric::SettingsHorizontalPadding);
    auto* form = new QFormLayout();
    form->setContentsMargins(inset, 0, inset, 0);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    return form;
}

// A settings row spans the whole width with its caption on the left and its control against the right edge.
void Components::addSettingsRow(QFormLayout* form, const QString& label, QWidget* field) {
    const Theme& theme = ThemeManager::instance().theme();
    auto* caption = new QLabel(label, field->parentWidget());
    // A caption asks for the width its own words need, so the column wraps only what is longer than the readable bound.
    const int unwrapped = caption->sizeHint().width();
    caption->setMinimumWidth(std::min(unwrapped, theme.metric(ThemeMetric::SettingsLabelMaximumWidth)));
    caption->setWordWrap(true);

    // A control that carries text grows to the readable width and still shrinks with a narrow window.
    const QSizePolicy::Policy horizontal = field->sizePolicy().horizontalPolicy();
    const bool carriesText = horizontal != QSizePolicy::Fixed && horizontal != QSizePolicy::Minimum;

    if (carriesText) {
        field->setMinimumWidth(theme.metric(ThemeMetric::SettingsControlMinimumWidth));
        field->setMaximumWidth(theme.metric(ThemeMetric::SettingsControlMaximumWidth));
    }

    // Every row is the same height whatever it carries, because one that shrinks around a toggle reads as a different kind of row.
    field->setMinimumHeight(ComponentsHelper::standardControlHeight(field->parentWidget()));

    auto* row = new QWidget(field->parentWidget());
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addStretch(1);
    rowLayout->addWidget(field, carriesText ? 1 : 0);
    form->addRow(caption, row);
}

QLabel* Components::hintLabel(const QString& text, QWidget* parent) {
    const Theme& theme = ThemeManager::instance().theme();
    auto* note = new QLabel(text, parent);
    note->setObjectName(QStringLiteral("settingsHint"));
    note->setWordWrap(true);
    note->setFont(theme.font(ThemeFont::Caption));
    QPalette palette = note->palette();
    palette.setColor(QPalette::WindowText, theme.color(ThemeColor::TextMuted));
    note->setPalette(palette);
    return note;
}

// The hint belongs under the field it explains rather than on a row of its own, so it starts where the field starts and sits against it.
QWidget* Components::fieldWithHint(QWidget* field, const QString& hint, QWidget* parent) {
    auto* stacked = new QWidget(parent);
    auto* stackedLayout = new QVBoxLayout(stacked);
    stackedLayout->setContentsMargins(0, 0, 0, 0);
    stackedLayout->setSpacing(ThemeManager::instance().theme().metric(ThemeMetric::ControlVerticalPadding));
    stackedLayout->addWidget(field);
    stackedLayout->addWidget(Components::hintLabel(hint, stacked));
    return stacked;
}

void Components::addSettingsRow(QFormLayout* form, const QString& label, QWidget* field, const QString& hint) {
    Components::addSettingsRow(form, label, field);

    if (hint.isEmpty()) {
        return;
    }

    auto* row = new QWidget(field->parentWidget());
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addStretch(1);
    rowLayout->addWidget(Components::hintLabel(hint, field->parentWidget()));
    form->addRow(QString{}, row);
}

// A stepper reads as a minus and a plus beside the value, because a pair of stacked arrows is unreadable at this size.
// Every numeric field steps through the same minus and plus pair, because stacked native arrows are unreadable and not flat.
QWidget* Components::stepperRow(QAbstractSpinBox* box, const Theme& theme, QWidget* parent) {
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* decrease = Components::toolButton(IconName::Minus, theme, QString{}, row);
    decrease->setObjectName(QStringLiteral("stepperButton"));
    auto* increase = Components::toolButton(IconName::Add, theme, QString{}, row);
    increase->setObjectName(QStringLiteral("stepperButton"));

    box->setParent(row);
    layout->addWidget(box, 1);
    layout->addWidget(decrease);
    layout->addWidget(increase);

    QObject::connect(decrease, &QToolButton::clicked, box, &QAbstractSpinBox::stepDown);
    QObject::connect(increase, &QToolButton::clicked, box, &QAbstractSpinBox::stepUp);
    return row;
}

SettingsActions Components::settingsActionRow(QWidget* parent) {
    const int inset = ThemeManager::instance().theme().metric(ThemeMetric::SettingsHorizontalPadding);
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("settingsActionRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(inset, 0, inset, 0);
    layout->setSpacing(6);
    return {row, layout};
}

// The surface blocks what is behind it without holding the event loop, and it is modal to the application rather than to its parent, because a dialog modal to its parent is a sheet with none of the buttons a window carries.
void Components::showDialogWindow(QDialog* dialog, const QString& title) {
    dialog->setWindowTitle(title);
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

// The minimum size of a dialog carries the message, so the surface grows and the fields above it keep their room.
void Components::growDialogToContents(QDialog* dialog) {
    QLayout* layout = dialog->layout();

    if (layout == nullptr) {
        return;
    }

    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->invalidate();
    layout->activate();
    dialog->resize(dialog->width(), std::max(dialog->height(), layout->sizeHint().height()));
}

// A section title is written in upper case so it never reads as one more caption of the form below it.
QLabel* Components::sectionTitleLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text.toUpper(), parent);
    label->setObjectName(QStringLiteral("settingsSectionTitle"));
    return label;
}

QLabel* Components::emptyStateLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("emptyState"));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    return label;
}

const QStringList& Components::monospacedFontFamilies() {
    // clang-format off
    static const QStringList families = [] {
        QStringList installedFamilies;
        for (const auto& family : QFontDatabase::families()) {
            if (QFontDatabase::isFixedPitch(family)) {
                installedFamilies.append(family);
            }
        }
        return installedFamilies;
    }();
    // clang-format on
    return families;
}

const QStringList& Components::preferredMonospacedFontFamilies() {
    // clang-format off
    static const QStringList families{
        QStringLiteral("Menlo"), QStringLiteral("SF Mono"), QStringLiteral("Monaco"), QStringLiteral("Cascadia Mono"), QStringLiteral("Cascadia Code"), QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Liberation Mono"), QStringLiteral("Noto Sans Mono"), QStringLiteral("Ubuntu Mono"), QStringLiteral("JetBrains Mono"), QStringLiteral("Fira Mono"), QStringLiteral("Source Code Pro"), QStringLiteral("Hack"), QStringLiteral("Inconsolata"), QStringLiteral("Courier New")
    };
    // clang-format on
    return families;
}

QString Components::defaultMonospacedFontFamily() {
    const QStringList& installed = Components::monospacedFontFamilies();

    for (const auto& family : Components::preferredMonospacedFontFamilies()) {
        if (installed.contains(family)) {
            return family;
        }
    }

    return installed.value(0);
}

QString Components::localTimestamp(const QDateTime& utcTimestamp) {
    return QLocale::system().toString(utcTimestamp.toLocalTime(), QLocale::ShortFormat);
}

int Components::longestWordWidth(const QString& text, const QFontMetrics& metrics) {
    int longest = 0;

    for (const auto& word : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        longest = std::max(longest, metrics.horizontalAdvance(word));
    }

    return longest;
}

Qt::TextFlag Components::labelWrapping(const QString& text, const QFontMetrics& metrics, int availableWidth) {
    return Components::longestWordWidth(text, metrics) <= availableWidth ? Qt::TextWordWrap : Qt::TextWrapAnywhere;
}

} // namespace workpane::ui
