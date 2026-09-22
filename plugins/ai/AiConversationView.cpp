#include "AiConversationView.h"

#include "AiPlugin.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/TabBar.h"
#include "ui/Theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace workpane::plugins::ai {

constexpr int composerMinimumHeight = 40;
constexpr int composerMaximumLines = 8;
constexpr int bubbleSpacing = 3;
constexpr int bubbleGroupSpacing = 10;
constexpr int bubbleRadius = 12;
constexpr int bubbleHorizontalPadding = 11;
constexpr int bubbleVerticalPadding = 7;
constexpr int avatarSpacing = 8;
constexpr int toolGlyphSpacing = 6;
constexpr int toolLineSpacing = 5;
constexpr qreal bubbleWidthShare = 0.70;
constexpr float bubbleFootnoteOpacity = 0.65F;
constexpr int nearTheEndThreshold = 80;

class AiConversationViewHelper final {
  public:
    static QString sentAt(const QDateTime& createdAtUtc);
    static void paintText(QWidget* widget, const QColor& ink);
    static void paintText(QWidget* widget, const QColor& ink, int pointSize);
    static void arrangeBubble(ui::RoundedSurface* bubble, int available);
    static int naturalWidth(const ui::RoundedSurface* bubble, int available);
    static QColor footnote(const QColor& ink);
};

// The time and the tools are read after the message, so they are written in the same ink at a lower weight.
QColor AiConversationViewHelper::footnote(const QColor& ink) {
    QColor faded = ink;
    faded.setAlphaF(bubbleFootnoteOpacity);
    return faded;
}

// A label that wraps reports the width of one word, so what the bubble really carries is measured from the text itself.
int AiConversationViewHelper::naturalWidth(const ui::RoundedSurface* bubble, int available) {
    int wanted = 0;

    for (const auto* entry : bubble->findChildren<QWidget*>(QStringLiteral("aiConversationTool"))) {
        for (const auto* label : entry->findChildren<QLabel*>()) {
            const int indent = label->contentsMargins().left() + label->contentsMargins().right();
            // The advance is measured without rounding it down, because a line given exactly the width it reports still wraps.
            const int text = static_cast<int>(std::ceil(QFontMetricsF(label->font()).horizontalAdvance(label->text())));
            wanted = std::max(wanted, indent + text);
        }
    }

    return std::min(wanted, available);
}

// The time reads at the end of the line it belongs to whenever that line has room, and moves to a line of its own when it does not.
void AiConversationViewHelper::arrangeBubble(ui::RoundedSurface* bubble, int available) {
    auto* line = bubble->findChild<QWidget*>(QStringLiteral("aiConversationLine"));
    auto* sent = bubble->findChild<QLabel*>(QStringLiteral("aiConversationTime"));
    auto* content = line != nullptr ? line->findChild<ui::MarkdownView*>(QStringLiteral("aiConversationContent")) : nullptr;

    auto* lineLayout = line != nullptr ? qobject_cast<QHBoxLayout*>(line->layout()) : nullptr;

    if (sent == nullptr || lineLayout == nullptr) {
        return;
    }

    const bool carriesTools = !bubble->findChildren<QWidget*>(QStringLiteral("aiConversationTool")).isEmpty();
    const int beside = sent->sizeHint().width() + bubbleHorizontalPadding;
    bool inlineTime = false;

    if (content != nullptr && content->isVisibleTo(bubble)) {
        content->fitTo(available);
        inlineTime = !carriesTools && content->document()->lineCount() <= 1 && content->width() + beside <= available;
        if (inlineTime) {
            content->fitTo(available - beside);
        }
    }

    const bool alreadyInline = lineLayout->indexOf(sent) >= 0;

    if (inlineTime != alreadyInline) {
        if (inlineTime) {
            bubble->content()->removeWidget(sent);
            lineLayout->addWidget(sent, 0, Qt::AlignBottom);
        } else {
            lineLayout->removeWidget(sent);
            bubble->content()->addWidget(sent);
        }
        sent->show();
    }

    // Every bubble is as wide as the words it carries, whether those words are a message or the tools a turn called.
    const int carried = std::max(content != nullptr && content->isVisibleTo(bubble) ? content->width() : 0, naturalWidth(bubble, available));
    bubble->setMinimumWidth(carried > 0 ? carried + 2 * bubbleHorizontalPadding : 0);

    lineLayout->invalidate();
    lineLayout->activate();
    bubble->content()->invalidate();
    bubble->content()->activate();
    bubble->updateGeometry();
}

void AiConversationViewHelper::paintText(QWidget* widget, const QColor& ink, int pointSize) {
    QFont reading = widget->font();
    reading.setPointSize(pointSize);
    widget->setFont(reading);
    paintText(widget, ink);
}

void AiConversationViewHelper::paintText(QWidget* widget, const QColor& ink) {
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Text, ink);
    palette.setColor(QPalette::WindowText, ink);
    palette.setColor(QPalette::Base, Qt::transparent);
    widget->setPalette(palette);
    widget->setAutoFillBackground(false);
}

QString AiConversationViewHelper::sentAt(const QDateTime& createdAtUtc) {
    return QLocale::system().toString(createdAtUtc.toLocalTime().time(), QLocale::ShortFormat);
}

AiConversationView::AiConversationView(AiPlugin& plugin, PluginHost& host, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_host(host), m_fontSize(plugin.executionSettings().chatFontSize) {
    setObjectName(QStringLiteral("aiConversationView"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_messages = new QWidget(this);
    m_messages->setObjectName(QStringLiteral("aiConversationMessages"));
    m_messages->setAttribute(Qt::WA_StyledBackground);
    m_messagesLayout = new QVBoxLayout(m_messages);
    m_messagesLayout->setContentsMargins(14, 12, 14, 12);
    m_messagesLayout->setSpacing(bubbleSpacing);
    m_messagesLayout->insertStretch(0, 1);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("aiConversationScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidget(m_messages);
    root->addWidget(m_scroll, 1);

    m_empty = ui::Components::emptyStateLabel(m_host.translate(QStringLiteral("ai.conversation.empty")), this);
    m_empty->setObjectName(QStringLiteral("aiConversationEmpty"));
    root->addWidget(m_empty, 1);

    root->addWidget(ui::Components::horizontalDivider(this));

    auto* composerRow = new QWidget(this);
    auto* composerLayout = new QHBoxLayout(composerRow);
    composerLayout->setContentsMargins(12, 8, 12, 8);
    composerLayout->setSpacing(8);
    m_composer = new ui::TextField(m_host.translate(QStringLiteral("ai.conversation.placeholder")), composerRow);
    m_composer->setObjectName(QStringLiteral("aiConversationComposer"));
    m_composer->setMinimumHeight(composerMinimumHeight);
    m_composer->setFixedHeight(composerMinimumHeight);
    m_composer->installEventFilter(this);
    m_send = new QPushButton(ui::IconCatalog::primaryIcon(ui::IconName::Forward, m_host.theme()), {}, composerRow);
    m_send->setObjectName(QStringLiteral("aiConversationSend"));
    m_send->setToolTip(m_host.translate(QStringLiteral("ai.conversation.send")));
    m_send->setFixedSize(m_host.theme().metric(ui::ThemeMetric::RoundButtonSize), m_host.theme().metric(ui::ThemeMetric::RoundButtonSize));
    composerLayout->addWidget(m_composer, 1);
    composerLayout->addWidget(m_send, 0, Qt::AlignBottom);
    root->addWidget(composerRow);

    // clang-format off
    connect(m_send, &QPushButton::clicked, this, [this]() { submit(); });
    connect(m_composer->document(), &QTextDocument::contentsChanged, this, [this]() { growComposer(); });
    connect(&m_plugin, &AiPlugin::conversationChanged, this, [this](const QString& taskId) { if (taskId == m_taskId) { rebuild(); } });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) { const QScrollBar* bar = m_scroll->verticalScrollBar(); m_stickToEnd = bar->maximum() - value <= nearTheEndThreshold; if (value == 0) { loadOlder(); } });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this]() { scrollToNewest(); });
    connect(&m_plugin, &AiPlugin::conversationStreamed, this, [this](const QString& taskId, const QString& text) { if (taskId == m_taskId) { showStreaming(text); } });
    connect(&m_plugin, &AiPlugin::taskRunStateChanged, this, [this](const QString& taskId) { if (taskId == m_taskId) { updateRunState(); } });
    connect(&m_plugin, &AiPlugin::executionSettingsChanged, this, [this]() { const int wanted = m_plugin.executionSettings().chatFontSize; if (wanted != m_fontSize) { m_fontSize = wanted; rebuild(); } });
    // clang-format on
}

const QString& AiConversationView::taskId() const {
    return m_taskId;
}

void AiConversationView::setTask(const QString& taskId) {
    m_taskId = taskId;
    m_composer->clear();
    m_followNewest = true;

    auto future = m_plugin.loadConversation(taskId);
    // clang-format off
    future.then(this, [this](Result<void> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), m_host.translate(QStringLiteral("ai.error.conversation-save")), AlertSeverity::Error); } rebuild(); });
    // clang-format on
    rebuild();
}

// Every message is rebuilt from the stored conversation, which is the only history there is.
void AiConversationView::rebuild() {
    const bool following = m_followNewest || m_stickToEnd;
    m_followNewest = false;

    while (m_messagesLayout->count() > 1) {
        QLayoutItem* item = m_messagesLayout->takeAt(m_messagesLayout->count() - 1);
        if (QWidget* widget = item->widget(); widget != nullptr) {
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }

    m_streaming = nullptr;
    m_pendingRow = nullptr;
    m_lastRole.reset();
    const QVector<ConversationMessage> conversation = m_plugin.conversation(m_taskId);
    QVector<ConversationMessage> results;

    for (const auto& message : conversation) {
        if (message.role == ConversationRole::Tool) {
            results.append(message);
        }
    }

    // A result is read inside the row of the call it answers, so it opens no bubble of its own.
    for (const auto& message : conversation) {
        if (message.role != ConversationRole::Tool) {
            appendBubble(message);
        }
    }

    m_scroll->setVisible(!conversation.isEmpty());
    m_empty->setVisible(conversation.isEmpty());
    applyBubbleWidths();
    updateRunState();
    m_stickToEnd = following;
    scrollToNewest();
    // clang-format off
    QTimer::singleShot(0, this, [this]() { applyBubbleWidths(); scrollToNewest(); });
    // clang-format on
}

// A message is a bubble on the side of whoever wrote it, and only the first of a group carries the tail.
ui::MarkdownView* AiConversationView::appendBubble(const ConversationMessage& message, bool pending) {
    const bool outgoing = message.role == ConversationRole::User;
    const bool grouped = m_lastRole.has_value() && m_lastRole.value() == message.role;
    const ui::Theme& theme = m_host.theme();
    const QColor fill = outgoing ? theme.color(ui::ThemeColor::AccentStrong) : theme.color(ui::ThemeColor::Raised);
    const QColor ink = outgoing ? theme.color(ui::ThemeColor::OnAccent) : theme.color(ui::ThemeColor::Text);

    auto* row = new QWidget(m_messages);
    row->setObjectName(QStringLiteral("aiConversationRow"));
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, grouped ? 0 : bubbleGroupSpacing, 0, 0);
    rowLayout->setSpacing(avatarSpacing);

    const int avatarSize = theme.metric(ui::ThemeMetric::RoundButtonSize) - avatarSpacing;
    auto* avatar = new ui::AvatarBadge(outgoing ? ui::IconName::Person : ui::IconName::Spark, outgoing ? theme.color(ui::ThemeColor::AccentStrong) : theme.color(ui::ThemeColor::Raised), ink, avatarSize, row);
    avatar->setObjectName(outgoing ? QStringLiteral("aiConversationYou") : QStringLiteral("aiConversationAgent"));
    QSizePolicy keepsItsRoom = avatar->sizePolicy();
    keepsItsRoom.setRetainSizeWhenHidden(true);
    avatar->setSizePolicy(keepsItsRoom);
    avatar->setVisible(!grouped);

    auto* bubble = new ui::RoundedSurface(fill, bubbleRadius, row);
    bubble->setObjectName(QStringLiteral("aiConversationBubble"));
    bubble->content()->setContentsMargins(bubbleHorizontalPadding, bubbleVerticalPadding, bubbleHorizontalPadding, bubbleVerticalPadding);
    bubble->setMaximumWidth(bubbleWidth());

    auto* line = new QWidget(bubble);
    line->setObjectName(QStringLiteral("aiConversationLine"));
    auto* lineLayout = new QHBoxLayout(line);
    lineLayout->setContentsMargins(0, 0, 0, 0);
    lineLayout->setSpacing(bubbleHorizontalPadding);
    bubble->content()->addWidget(line);

    auto* content = new ui::MarkdownView(theme, line);
    content->setObjectName(QStringLiteral("aiConversationContent"));
    content->setContentFontSize(m_fontSize);
    content->setInk(ink);
    content->setChatMarkdown(message.content);
    lineLayout->addWidget(content, 0, Qt::AlignLeft);
    // A turn that carries no words yet takes no room for them, otherwise the box is taller at the top than at the bottom.
    line->setVisible(!message.content.trimmed().isEmpty());

    for (const auto& value : message.toolCalls) {
        appendToolRow(bubble, value.toObject(), ink);
    }

    if (pending) {
        auto* thinking = new QWidget(bubble);
        thinking->setObjectName(QStringLiteral("aiConversationThinking"));
        auto* thinkingLayout = new QHBoxLayout(thinking);
        thinkingLayout->setContentsMargins(0, 0, 0, 0);
        thinkingLayout->setSpacing(6);
        auto* busy = new ui::BusyIndicator(theme, thinking);
        busy->setObjectName(QStringLiteral("aiConversationBusy"));
        auto* phase = new QLabel(thinking);
        phase->setObjectName(QStringLiteral("aiConversationPhase"));
        AiConversationViewHelper::paintText(phase, AiConversationViewHelper::footnote(ink), m_fontSize);
        thinkingLayout->addWidget(busy);
        thinkingLayout->addWidget(phase);
        thinkingLayout->addStretch(1);
        bubble->content()->addWidget(thinking);
    }

    auto* sent = new QLabel(AiConversationViewHelper::sentAt(message.createdAtUtc), bubble);
    sent->setObjectName(QStringLiteral("aiConversationTime"));
    sent->setAlignment(Qt::AlignRight | Qt::AlignBottom);
    AiConversationViewHelper::paintText(sent, AiConversationViewHelper::footnote(ink), m_fontSize);
    bubble->content()->addWidget(sent);

    if (outgoing) {
        rowLayout->addStretch(1);
        rowLayout->addWidget(bubble);
        rowLayout->addWidget(avatar, 0, Qt::AlignTop);
    } else {
        rowLayout->addWidget(avatar, 0, Qt::AlignTop);
        rowLayout->addWidget(bubble);
        rowLayout->addStretch(1);
    }

    m_lastRole = message.role;
    m_messagesLayout->addWidget(row);
    return content;
}

// A call is read as the name the tool publishes with the one thing it is doing under it, never as the arguments it was given.
void AiConversationView::appendToolRow(QWidget* bubble, const QJsonObject& call, const QColor& ink) {
    const ToolPresentation presented = m_plugin.toolPresentation(call.value(QStringLiteral("name")).toString(), call.value(QStringLiteral("arguments")).toObject());
    const int glyph = m_host.theme().metric(ui::ThemeMetric::SmallIconSize);

    auto* entry = new QWidget(bubble);
    entry->setObjectName(QStringLiteral("aiConversationTool"));
    auto* entryLayout = new QVBoxLayout(entry);
    entryLayout->setContentsMargins(0, 0, 0, 0);
    entryLayout->setSpacing(toolLineSpacing);

    auto* titleRow = new QWidget(entry);
    auto* titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(toolGlyphSpacing);

    auto* mark = new QLabel(titleRow);
    mark->setObjectName(QStringLiteral("aiConversationToolIcon"));
    mark->setFixedSize(glyph, glyph);
    mark->setPixmap(ui::IconCatalog::icon(ui::IconName::Tool, m_host.theme().color(ui::ThemeColor::Accent)).pixmap(glyph, glyph));

    auto* name = new QLabel(presented.title, titleRow);
    name->setObjectName(QStringLiteral("aiConversationToolName"));
    AiConversationViewHelper::paintText(name, m_host.theme().color(ui::ThemeColor::Accent), m_fontSize);

    titleLayout->addWidget(mark);
    titleLayout->addWidget(name);
    titleLayout->addStretch(1);
    entryLayout->addWidget(titleRow);

    auto* activity = new QLabel(presented.activity, entry);
    activity->setObjectName(QStringLiteral("aiConversationToolActivity"));
    activity->setWordWrap(true);
    activity->setContentsMargins(glyph + toolGlyphSpacing, 0, 0, 0);
    activity->setVisible(!presented.activity.isEmpty());
    AiConversationViewHelper::paintText(activity, AiConversationViewHelper::footnote(ink), m_fontSize);
    entryLayout->addWidget(activity);

    bubble->layout()->addWidget(entry);
}

// The bubble never takes the whole width, because a wall of text edge to edge is not a conversation.
int AiConversationView::bubbleWidth() const {
    const int available = m_scroll->viewport()->width() > 0 ? m_scroll->viewport()->width() : width();
    return static_cast<int>(available * bubbleWidthShare);
}

// The bubbles reflow with the window, so a conversation read in a narrow shell wraps where that shell ends.
void AiConversationView::applyBubbleWidths() {
    const int wide = bubbleWidth();

    for (auto* bubble : m_messages->findChildren<ui::RoundedSurface*>(QStringLiteral("aiConversationBubble"))) {
        bubble->setMaximumWidth(wide);
    }

    for (auto* bubble : m_messages->findChildren<QWidget*>(QStringLiteral("aiConversationBubble"))) {
        AiConversationViewHelper::arrangeBubble(qobject_cast<ui::RoundedSurface*>(bubble), wide - 2 * bubbleHorizontalPadding);
    }
}

void AiConversationView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    applyBubbleWidths();
}

// The page is given its width when it becomes the one on screen, which is when the bubbles first have room to reflow into.
void AiConversationView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    applyBubbleWidths();
    scrollToNewest();
}

// The composer opens on one line and grows with every line written into it until the bound it may not pass.
void AiConversationView::growComposer() {
    const int lines = std::clamp(static_cast<int>(m_composer->document()->lineCount()), 1, composerMaximumLines);
    const int wanted = composerMinimumHeight + (lines - 1) * m_composer->fontMetrics().lineSpacing();
    m_composer->setFixedHeight(wanted);
}

// Scrolling to the top asks for the page before it, and the view keeps the reader where they were reading.
void AiConversationView::loadOlder() {
    if (m_loadingOlder) {
        return;
    }

    m_loadingOlder = true;
    auto future = m_plugin.loadOlderConversation(m_taskId);
    // clang-format off
    future.then(this, [this](Result<bool> result) { m_loadingOlder = false; if (result.hasValue() && result.value()) { m_scroll->verticalScrollBar()->setValue(1); } });
    // clang-format on
}

// The answer is shown while it is being written, and the stored turn replaces it once it lands.
// The turn being written opens its own bubble, so what the agent is doing is read where its answer will be.
void AiConversationView::openPendingTurn() {
    if (m_pendingRow != nullptr) {
        return;
    }

    ConversationMessage live;
    live.role = ConversationRole::Assistant;
    live.createdAtUtc = QDateTime::currentDateTimeUtc();
    m_streaming = appendBubble(live, true);
    m_pendingRow = m_messagesLayout->itemAt(m_messagesLayout->count() - 1)->widget();
    m_scroll->setVisible(true);
    m_empty->setVisible(false);
    applyBubbleWidths();
}

void AiConversationView::closePendingTurn() {
    if (m_pendingRow == nullptr) {
        return;
    }

    m_messagesLayout->removeWidget(m_pendingRow);
    m_pendingRow->setParent(nullptr);
    m_pendingRow->deleteLater();
    m_pendingRow = nullptr;
    m_streaming = nullptr;
}

void AiConversationView::showStreaming(const QString& text) {
    openPendingTurn();

    if (m_streaming == nullptr) {
        return;
    }

    m_streaming->parentWidget()->setVisible(true);
    m_streaming->setChatMarkdown(text);

    if (auto* thinking = m_pendingRow->findChild<QWidget*>(QStringLiteral("aiConversationThinking")); thinking != nullptr) {
        thinking->hide();
    }

    applyBubbleWidths();
    scrollToNewest();
}

// A reader who is at the end is following the conversation, so the view stays there through every pass that resizes it.
void AiConversationView::scrollToNewest() {
    if (!m_stickToEnd) {
        return;
    }

    m_messages->layout()->activate();
    m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
}

// A turn that is still running says so inside the bubble it is writing, because a silence reads as a finished run.
void AiConversationView::updateRunState() {
    const bool running = m_plugin.runState(m_taskId) != TaskRunState::Idle;

    if (!running) {
        closePendingTurn();
        return;
    }

    openPendingTurn();
    auto* thinking = m_pendingRow->findChild<QWidget*>(QStringLiteral("aiConversationThinking"));
    auto* busy = m_pendingRow->findChild<ui::BusyIndicator*>(QStringLiteral("aiConversationBusy"));
    auto* phase = m_pendingRow->findChild<QLabel*>(QStringLiteral("aiConversationPhase"));
    auto* sent = m_pendingRow->findChild<QLabel*>(QStringLiteral("aiConversationTime"));

    if (thinking == nullptr || busy == nullptr || phase == nullptr || sent == nullptr) {
        return;
    }

    const bool written = m_streaming != nullptr && !m_streaming->toPlainText().trimmed().isEmpty();
    thinking->setVisible(!written);
    busy->setRunning(!written);
    sent->setVisible(written);
    phase->setText(m_host.translate(QStringLiteral("ai.phase.") + AiPlugin::phaseName(m_plugin.executionPhase(m_taskId))));
    // The phase it reached is a longer or a shorter sentence, so the row is measured again against the one it now carries.
    thinking->layout()->invalidate();
    thinking->layout()->activate();
    applyBubbleWidths();
}

void AiConversationView::submit() {
    const QString text = m_composer->toPlainText().trimmed();

    if (text.isEmpty()) {
        return;
    }

    m_composer->clear();
    m_followNewest = true;
    auto future = m_plugin.sendMessage(m_taskId, text);
    // clang-format off
    future.then(this, [this](Result<void> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), m_host.translate(QStringLiteral("ai.error.conversation-save")), AlertSeverity::Error); } });
    // clang-format on
}

void AiConversationView::resetConversation() {
    if (!m_host.confirm(this, m_host.translate(QStringLiteral("ai.conversation.reset-title")), m_host.translate(QStringLiteral("ai.conversation.reset-message")), m_host.translate(QStringLiteral("ai.conversation.reset-detail")), m_host.translate(QStringLiteral("ai.conversation.reset")), true)) {
        return;
    }

    auto future = m_plugin.resetConversation(m_taskId);
    // clang-format off
    future.then(this, [this](Result<void> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), m_host.translate(QStringLiteral("ai.error.conversation-save")), AlertSeverity::Error); } });
    // clang-format on
}

// The composer sends on the plain return and breaks the line on the shifted one, which is what every chat does.
bool AiConversationView::eventFilter(QObject* watched, QEvent* event) {
    if (watched != m_composer || event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);

    if ((keyEvent->key() != Qt::Key_Return && keyEvent->key() != Qt::Key_Enter) || keyEvent->modifiers().testFlag(Qt::ShiftModifier)) {
        return QWidget::eventFilter(watched, event);
    }

    submit();
    return true;
}

} // namespace workpane::plugins::ai
