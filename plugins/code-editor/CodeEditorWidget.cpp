#include "CodeEditorWidget.h"

#include "ui/Components.h"
#include "ui/Theme.h"

#include <QAbstractItemView>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolTip>

#include <algorithm>

namespace workpane::plugins::codeeditor {

class CodeEditorWidgetHelper final {
  public:
    static QTextCursor rangeCursor(const QTextDocument& document, int startLine, int startCharacter, int endLine, int endCharacter);
    static QColor severityColor(const ui::Theme& theme, int severity);
};

QTextCursor CodeEditorWidgetHelper::rangeCursor(const QTextDocument& document, int startLine, int startCharacter, int endLine, int endCharacter) {
    const QTextBlock start = document.findBlockByNumber(std::max(0, startLine));
    const QTextBlock end = document.findBlockByNumber(std::max(0, endLine));

    if (!start.isValid() || !end.isValid()) {
        return {};
    }

    QTextCursor cursor(start);
    cursor.setPosition(std::min(start.position() + std::max(0, startCharacter), start.position() + start.length() - 1));
    cursor.setPosition(std::min(end.position() + std::max(0, endCharacter), end.position() + end.length() - 1), QTextCursor::KeepAnchor);
    return cursor;
}

QColor CodeEditorWidgetHelper::severityColor(const ui::Theme& theme, int severity) {
    if (severity == 1) {
        return theme.color(ui::ThemeColor::Danger);
    }
    if (severity == 2) {
        return theme.color(ui::ThemeColor::Warning);
    }

    return theme.color(ui::ThemeColor::TextMuted);
}

CodeEditorWidget::CodeEditorWidget(const ui::Theme& theme, const CodeColorScheme& scheme, QWidget* parent) : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(*this)), m_theme(theme) {
    setColorScheme(scheme);
    QFont editorFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    editorFont.setFixedPitch(true);
    setFont(editorFont);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setIndentation(m_indentStyle, m_indentWidth);

    // clang-format off
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this]() { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rectangle, int verticalDelta) { updateLineNumberArea(rectangle, verticalDelta); });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() { highlightCurrentLine(); });
    // clang-format on
    m_completer = new QCompleter(this);
    m_proposalModel = new QStandardItemModel(m_completer);
    m_completer->setModel(m_proposalModel);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->popup()->setObjectName(QStringLiteral("codeEditorCompletion"));
    // clang-format off
    connect(m_completer, qOverload<const QModelIndex&>(&QCompleter::activated), this, [this](const QModelIndex& index) { const int row = index.data(Qt::UserRole).toInt(); if (row >= 0 && row < m_proposals.size()) { applyProposal(m_proposals.at(row)); } });
    connect(m_completer, qOverload<const QModelIndex&>(&QCompleter::highlighted), this, [this](const QModelIndex& index) { presentDocumentation(index.data(Qt::UserRole).toInt()); });
    // clang-format on
    updateLineNumberAreaWidth();
    highlightCurrentLine();
    viewport()->installEventFilter(this);
}

int CodeEditorWidget::lineNumberAreaWidth() const {
    int digits = 1;

    for (int lines = std::max(1, blockCount()); lines >= 10; lines /= 10) {
        ++digits;
    }

    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditorWidget::paintLineNumbers(QPaintEvent* event) {
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), m_scheme.surface.lineNumberBackground);

    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(number == textCursor().blockNumber() ? m_scheme.surface.currentLineNumber : m_scheme.surface.lineNumber);
            painter.drawText(0, top, m_lineNumberArea->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void CodeEditorWidget::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    m_lineNumberArea->setGeometry(QRect(area.left(), area.top(), lineNumberAreaWidth(), area.height()));
}

IndentStyle CodeEditorWidget::indentStyle() const {
    return m_indentStyle;
}

int CodeEditorWidget::indentWidth() const {
    return m_indentWidth;
}

void CodeEditorWidget::setEditorFont(const QString& family, int pointSize) {
    const QString resolved = family.isEmpty() ? ui::Components::defaultMonospacedFontFamily() : family;
    QFont editorFont = resolved.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QFont(resolved);
    editorFont.setFixedPitch(true);
    editorFont.setStyleHint(QFont::Monospace);
    editorFont.setPointSize(pointSize);
    setFont(editorFont);
    updateLineNumberAreaWidth();
    viewport()->update();
}

void CodeEditorWidget::setIndentation(IndentStyle style, int width) {
    m_indentStyle = style;
    m_indentWidth = std::max(1, width);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * m_indentWidth);
}

void CodeEditorWidget::setWordWrap(bool enabled) {
    setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void CodeEditorWidget::keyPressEvent(QKeyEvent* event) {
    // The open proposal list owns navigation and acceptance, otherwise the editor would answer them first and write a line break instead of the proposal.
    static const QVector<int> popupKeys{Qt::Key_Enter, Qt::Key_Return, Qt::Key_Tab, Qt::Key_Backtab, Qt::Key_Escape, Qt::Key_Up, Qt::Key_Down, Qt::Key_PageUp, Qt::Key_PageDown};

    if (m_completer->popup()->isVisible() && popupKeys.contains(event->key())) {
        event->ignore();
        return;
    }

    if (event->matches(QKeySequence::Save)) {
        emit saveRequested();
        return;
    }

    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::ControlModifier) {
        emit completionRequested(textCursor().blockNumber(), textCursor().positionInBlock());
        return;
    }

    if (event->key() == Qt::Key_F12 && event->modifiers() == Qt::NoModifier) {
        emit definitionRequested(textCursor().blockNumber(), textCursor().positionInBlock());
        return;
    }

    if (event->key() == Qt::Key_F12 && event->modifiers() == Qt::ShiftModifier) {
        emit referencesRequested(textCursor().blockNumber(), textCursor().positionInBlock());
        return;
    }

    if (event->matches(QKeySequence::Find)) {
        emit findRequested();
        return;
    }

    if (event->matches(QKeySequence::FindNext)) {
        emit findNextRequested(true);
        return;
    }

    if (event->matches(QKeySequence::FindPrevious)) {
        emit findNextRequested(false);
        return;
    }

    if (event->key() == Qt::Key_Backtab || (event->key() == Qt::Key_Tab && event->modifiers() == Qt::ShiftModifier)) {
        indentSelection(false);
        return;
    }

    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        if (textCursor().hasSelection()) {
            indentSelection(true);
            return;
        }
        insertPlainText(indentText());
        return;
    }

    QPlainTextEdit::keyPressEvent(event);

    if (!m_completer->popup()->isVisible()) {
        return;
    }
    // A list the server declared incomplete is asked for again rather than narrowed here, because what it left out depends on what is typed.
    if (m_completionsIncomplete) {
        emit completionRequested(textCursor().blockNumber(), textCursor().positionInBlock());
        return;
    }

    m_completer->setCompletionPrefix(completionPrefix());

    if (m_completer->completionCount() == 0) {
        hideCompletions();
        return;
    }

    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
}

QString CodeEditorWidget::indentText() const {
    return m_indentStyle == IndentStyle::Tab ? QStringLiteral("\t") : QString(m_indentWidth, QLatin1Char(' '));
}

void CodeEditorWidget::indentSelection(bool forward) {
    QTextCursor cursor = textCursor();
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    cursor.setPosition(selectionStart);
    const int firstBlock = cursor.blockNumber();
    cursor.setPosition(selectionEnd);
    const int lastBlock = cursor.blockNumber();

    const QString indentation = indentText();
    cursor.beginEditBlock();

    for (int number = firstBlock; number <= lastBlock; ++number) {
        QTextCursor lineCursor(document()->findBlockByNumber(number));
        if (forward) {
            lineCursor.insertText(indentation);
            continue;
        }
        const QString line = lineCursor.block().text();
        int columns = 0;
        int characters = 0;
        while (characters < line.size() && columns < m_indentWidth && (line.at(characters) == QLatin1Char(' ') || line.at(characters) == QLatin1Char('\t'))) {
            columns += line.at(characters) == QLatin1Char('\t') ? m_indentWidth : 1;
            ++characters;
        }
        lineCursor.setPosition(lineCursor.block().position() + characters, QTextCursor::KeepAnchor);
        lineCursor.removeSelectedText();
    }

    cursor.endEditBlock();
}

// The server decides what is written and where, because a label carries the signature a user reads and never the text that compiles.
void CodeEditorWidget::showCompletions(const QVector<CompletionProposal>& proposals, bool incomplete) {
    m_proposals = proposals;
    m_completionsIncomplete = incomplete;

    if (m_proposals.isEmpty()) {
        hideCompletions();
        return;
    }

    m_proposalModel->clear();

    for (qsizetype row = 0; row < m_proposals.size(); ++row) {
        const auto& proposal = m_proposals.at(row);
        auto* entry = new QStandardItem(proposal.detail.isEmpty() ? proposal.label : proposal.label + QStringLiteral("   ") + proposal.detail);
        entry->setData(proposal.filterText, Qt::EditRole);
        entry->setData(static_cast<int>(row), Qt::UserRole);
        entry->setToolTip(proposal.documentation);
        m_proposalModel->appendRow(entry);
    }

    m_completer->setCompletionPrefix(completionPrefix());

    if (m_completer->completionCount() == 0) {
        hideCompletions();
        return;
    }

    QRect area = cursorRect();
    area.setWidth(m_completer->popup()->sizeHintForColumn(0) + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 24);
    m_completer->complete(area);
    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    presentDocumentation(0);
}

void CodeEditorWidget::setCompletionDocumentation(int row, const QString& documentation) {
    if (row < 0 || row >= m_proposals.size()) {
        return;
    }

    m_proposals[row].documentation = documentation;

    if (m_completer->popup()->isVisible() && m_completer->popup()->currentIndex().data(Qt::UserRole).toInt() == row) {
        presentDocumentation(row);
    }
}

// The documentation belongs beside the popup, because a tooltip over the list would cover the very row it explains.
void CodeEditorWidget::presentDocumentation(int row) {
    if (row < 0 || row >= m_proposals.size()) {
        return;
    }

    emit completionDocumentationRequested(row);
    const QString text = m_proposals.at(row).documentation.isEmpty() ? m_proposals.at(row).detail : m_proposals.at(row).documentation;

    if (text.isEmpty()) {
        QToolTip::hideText();
        return;
    }

    QToolTip::showText(m_completer->popup()->mapToGlobal(QPoint(m_completer->popup()->width() + 4, 0)), text, m_completer->popup());
}

// A range the document no longer has would resolve to the first character, so the word under the cursor answers for it instead.
void CodeEditorWidget::applyProposal(const CompletionProposal& proposal) {
    QTextCursor cursor = proposal.hasRange ? cursorForRange(proposal.startLine, proposal.startCharacter, proposal.endLine, proposal.endCharacter) : QTextCursor{};

    if (cursor.isNull()) {
        cursor = textCursor();
        cursor.select(QTextCursor::WordUnderCursor);
    }

    cursor.insertText(proposal.insertText);
    setTextCursor(cursor);
}

QTextCursor CodeEditorWidget::cursorForRange(int startLine, int startCharacter, int endLine, int endCharacter) const {
    return CodeEditorWidgetHelper::rangeCursor(*document(), startLine, startCharacter, endLine, endCharacter);
}

QString CodeEditorWidget::completionPrefix() const {
    QTextCursor cursor = textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor.selectedText();
}

// The active parameter is the one the caller is typing, so it is the signature that must be visible while the call is written.
void CodeEditorWidget::showSignatureHelp(const SignatureHelpInfo& help) {
    if (help.signatures.isEmpty()) {
        return;
    }

    const int index = std::clamp(help.activeSignature, 0, static_cast<int>(help.signatures.size()) - 1);
    QToolTip::showText(mapToGlobal(cursorRect().bottomLeft()), help.signatures.at(index), viewport());
}

// The documentation is shown beside the list, so it has to leave with the list it explains.
void CodeEditorWidget::hideCompletions() {
    m_completer->popup()->hide();
    QToolTip::hideText();
}

void CodeEditorWidget::showHover(const QString& contents) {
    if (contents.isEmpty()) {
        return;
    }

    QToolTip::showText(viewport()->mapToGlobal(m_hoverPoint), contents, viewport());
}

void CodeEditorWidget::mouseReleaseEvent(QMouseEvent* event) {
    QPlainTextEdit::mouseReleaseEvent(event);

    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ControlModifier) {
        emitPositionRequest(event->position().toPoint(), true);
    }
}

bool CodeEditorWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == viewport() && event->type() == QEvent::ToolTip) {
        m_hoverPoint = static_cast<QHelpEvent*>(event)->pos();
        const QString diagnostic = diagnosticAt(cursorForPosition(m_hoverPoint));
        if (!diagnostic.isEmpty()) {
            showHover(diagnostic);
            event->accept();
            return true;
        }
        emitPositionRequest(m_hoverPoint, false);
        event->accept();
        return true;
    }

    return QPlainTextEdit::eventFilter(watched, event);
}

void CodeEditorWidget::emitPositionRequest(const QPoint& viewportPoint, bool definition) {
    const QTextCursor cursor = cursorForPosition(viewportPoint);

    if (definition) {
        emit definitionRequested(cursor.blockNumber(), cursor.positionInBlock());
        return;
    }

    emit hoverRequested(cursor.blockNumber(), cursor.positionInBlock());
}

void CodeEditorWidget::updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditorWidget::updateLineNumberArea(const QRect& rectangle, int verticalDelta) {
    if (verticalDelta != 0) {
        m_lineNumberArea->scroll(0, verticalDelta);
    } else {
        m_lineNumberArea->update(0, rectangle.y(), m_lineNumberArea->width(), rectangle.height());
    }

    if (rectangle.contains(viewport()->rect())) {
        updateLineNumberAreaWidth();
    }
}

void CodeEditorWidget::highlightCurrentLine() {
    refreshExtraSelections();
    m_lineNumberArea->update();
}

void CodeEditorWidget::setDiagnostics(const QVector<LanguageDiagnostic>& diagnostics) {
    m_diagnostics = diagnostics;
    m_diagnosticSelections.clear();

    for (const auto& diagnostic : m_diagnostics) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = cursorForRange(diagnostic.startLine, diagnostic.startCharacter, diagnostic.endLine, diagnostic.endCharacter);
        if (selection.cursor.isNull() || !selection.cursor.hasSelection()) {
            continue;
        }
        selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selection.format.setUnderlineColor(CodeEditorWidgetHelper::severityColor(m_theme, diagnostic.severity));

        if (diagnostic.unnecessary) {
            selection.format.setForeground(m_theme.color(ui::ThemeColor::TextMuted));
        }
        if (diagnostic.deprecated) {
            selection.format.setFontStrikeOut(true);
        }

        m_diagnosticSelections.append(selection);
    }

    refreshExtraSelections();
}

void CodeEditorWidget::setOccurrences(const QVector<SourceLocation>& occurrences) {
    QColor tint = m_scheme.color(HighlightRole::Function);
    tint.setAlphaF(0.25F);
    m_occurrenceSelections.clear();

    for (const auto& entry : occurrences) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = cursorForRange(entry.line, entry.character, entry.endLine, entry.endCharacter);
        if (selection.cursor.isNull() || !selection.cursor.hasSelection()) {
            continue;
        }
        selection.format.setBackground(tint);
        m_occurrenceSelections.append(selection);
    }

    refreshExtraSelections();
}

void CodeEditorWidget::setSearchMatches(const QVector<QPair<int, int>>& matches) {
    QColor tint = m_scheme.color(HighlightRole::Constant);
    tint.setAlphaF(0.35F);
    m_searchSelections.clear();

    for (const auto& entry : matches) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(document());
        selection.cursor.setPosition(entry.first);
        selection.cursor.setPosition(entry.first + entry.second, QTextCursor::KeepAnchor);
        selection.format.setBackground(tint);
        m_searchSelections.append(selection);
    }

    refreshExtraSelections();
}

// The cursor moves on every keystroke, so only the current line is rebuilt and every other mark is kept as it already is.
void CodeEditorWidget::refreshExtraSelections() {
    QTextEdit::ExtraSelection current;
    current.format.setBackground(m_scheme.surface.currentLine);
    current.format.setProperty(QTextFormat::FullWidthSelection, true);
    current.cursor = textCursor();
    current.cursor.clearSelection();

    QList<QTextEdit::ExtraSelection> selections{current};
    selections.append(m_occurrenceSelections);
    selections.append(m_searchSelections);
    selections.append(m_diagnosticSelections);
    setExtraSelections(selections);
}

QString CodeEditorWidget::diagnosticAt(const QTextCursor& cursor) const {
    QStringList messages;

    for (const auto& diagnostic : m_diagnostics) {
        const QTextCursor range = CodeEditorWidgetHelper::rangeCursor(*document(), diagnostic.startLine, diagnostic.startCharacter, diagnostic.endLine, diagnostic.endCharacter);
        if (cursor.position() >= range.selectionStart() && cursor.position() <= range.selectionEnd()) {
            messages.append(diagnostic.message);
        }
    }

    return messages.join(QStringLiteral("\n"));
}

LineNumberArea::LineNumberArea(CodeEditorWidget& editor) : QWidget(&editor), m_editor(editor) {}

QSize LineNumberArea::sizeHint() const {
    return {m_editor.lineNumberAreaWidth(), 0};
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
    m_editor.paintLineNumbers(event);
}

// The scheme owns the surface the code is read on, so the palette of the editor follows it rather than the application theme.
void CodeEditorWidget::setColorScheme(const CodeColorScheme& scheme) {
    m_scheme = scheme;
    // The shared sheet paints every text edit in the window colour, so the surface of a scheme is declared on this widget where it wins.
    setStyleSheet(QStringLiteral("QPlainTextEdit { background: %1; color: %2; selection-background-color: %3; selection-color: %4; border: none; }").arg(m_scheme.surface.background.name(), m_scheme.color(HighlightRole::Text).name(), m_scheme.surface.selection.name(), m_scheme.surface.selectionText.name()));
    refreshExtraSelections();
    m_lineNumberArea->update();
    viewport()->update();
}

} // namespace workpane::plugins::codeeditor
