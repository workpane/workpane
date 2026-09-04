#include "ui/FindBar.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

#include <utility>

namespace workpane::ui {

FindBar::FindBar(const Theme& theme, FindBarLabels labels, QWidget* parent) : QWidget(parent), m_labels(std::move(labels)) {
    setObjectName(QStringLiteral("findBar"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(6);

    m_query = new QLineEdit(this);
    m_query->setObjectName(QStringLiteral("findQuery"));
    m_query->setPlaceholderText(m_labels.placeholder);
    m_query->setClearButtonEnabled(true);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("mutedLabel"));

    m_caseSensitive = new QToolButton(this);
    m_caseSensitive->setObjectName(QStringLiteral("findCase"));
    m_caseSensitive->setCheckable(true);
    m_caseSensitive->setText(QStringLiteral("Aa"));
    m_caseSensitive->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_caseSensitive->setToolTip(m_labels.caseSensitive);
    m_wholeWord = new QToolButton(this);
    m_wholeWord->setObjectName(QStringLiteral("findWholeWord"));
    m_wholeWord->setCheckable(true);
    m_wholeWord->setText(QStringLiteral("ab"));
    m_wholeWord->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_wholeWord->setToolTip(m_labels.wholeWord);

    auto* previous = new QToolButton(this);
    previous->setObjectName(QStringLiteral("findPrevious"));
    previous->setIcon(IconCatalog::icon(IconName::Back, theme));
    previous->setToolTip(m_labels.previous);
    auto* next = new QToolButton(this);
    next->setObjectName(QStringLiteral("findNext"));
    next->setIcon(IconCatalog::icon(IconName::Forward, theme));
    next->setToolTip(m_labels.next);
    auto* close = new QToolButton(this);
    close->setObjectName(QStringLiteral("findClose"));
    close->setIcon(IconCatalog::icon(IconName::Close, theme));
    close->setToolTip(m_labels.close);

    layout->addWidget(m_query, 1);
    layout->addWidget(m_status);
    layout->addWidget(m_caseSensitive);
    layout->addWidget(m_wholeWord);
    layout->addWidget(previous);
    layout->addWidget(next);
    layout->addWidget(close);

    m_query->installEventFilter(this);

    // clang-format off
    connect(m_query, &QLineEdit::textChanged, this, [this]() { emit queryChanged(); });
    connect(m_caseSensitive, &QToolButton::toggled, this, [this]() { emit queryChanged(); });
    connect(m_wholeWord, &QToolButton::toggled, this, [this]() { emit queryChanged(); });
    connect(previous, &QToolButton::clicked, this, [this]() { emit searchRequested(m_query->text(), false); });
    connect(next, &QToolButton::clicked, this, [this]() { emit searchRequested(m_query->text(), true); });
    connect(close, &QToolButton::clicked, this, [this]() { emit dismissed(); });
    // clang-format on
}

QString FindBar::query() const {
    return m_query->text();
}

bool FindBar::caseSensitive() const {
    return m_caseSensitive->isChecked();
}

bool FindBar::wholeWord() const {
    return m_wholeWord->isChecked();
}

void FindBar::activate(const QString& selectedText) {
    if (!selectedText.isEmpty() && !selectedText.contains(QLatin1Char('\n')) && !selectedText.contains(QChar::ParagraphSeparator)) {
        m_query->setText(selectedText);
    }

    show();
    m_query->setFocus();
    m_query->selectAll();
    emit queryChanged();
}

// The reader needs to know how many there are and which one is under the cursor, because a match alone says nothing about the rest of the file.
void FindBar::reportMatches(int current, int total, bool capped) {
    if (m_query->text().isEmpty()) {
        m_status->clear();
        return;
    }

    if (total <= 0) {
        m_status->setText(m_labels.notFound);
        return;
    }

    m_status->setText(QStringLiteral("%1/%2%3").arg(current).arg(total).arg(capped ? QStringLiteral("+") : QString{}));
}

bool FindBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched != m_query || event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);

    if (keyEvent->key() == Qt::Key_Escape) {
        emit dismissed();
        return true;
    }

    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        emit searchRequested(m_query->text(), !keyEvent->modifiers().testFlag(Qt::ShiftModifier));
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace workpane::ui
