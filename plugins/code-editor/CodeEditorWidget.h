#pragma once

#include "CodeColorScheme.h"
#include "EditorConfig.h"
#include "LanguageServerClient.h"

#include <QCompleter>
#include <QPlainTextEdit>
#include <QTextEdit>

class QStandardItemModel;

namespace workpane::ui {
class Theme;
}

namespace workpane::plugins::codeeditor {

class LineNumberArea;

class CodeEditorWidget final : public QPlainTextEdit {
    Q_OBJECT

  public:
    CodeEditorWidget(const ui::Theme& theme, const CodeColorScheme& scheme, QWidget* parent = nullptr);

    void setEditorFont(const QString& family, int pointSize);

    [[nodiscard]] int lineNumberAreaWidth() const;
    [[nodiscard]] IndentStyle indentStyle() const;
    [[nodiscard]] int indentWidth() const;
    void paintLineNumbers(QPaintEvent* event);
    void showCompletions(const QVector<CompletionProposal>& proposals, bool incomplete);
    void setCompletionDocumentation(int row, const QString& documentation);
    void showHover(const QString& contents);
    void showSignatureHelp(const SignatureHelpInfo& help);
    void setDiagnostics(const QVector<LanguageDiagnostic>& diagnostics);
    void setOccurrences(const QVector<SourceLocation>& occurrences);
    void setSearchMatches(const QVector<QPair<int, int>>& matches);
    void setIndentation(IndentStyle style, int width);
    void setWordWrap(bool enabled);
    void setColorScheme(const CodeColorScheme& scheme);

  signals:
    void saveRequested();
    void findRequested();
    void findNextRequested(bool forward);
    void completionRequested(int line, int character);
    void completionDocumentationRequested(int row);
    void definitionRequested(int line, int character);
    void referencesRequested(int line, int character);
    void hoverRequested(int line, int character);

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rectangle, int verticalDelta);
    void highlightCurrentLine();
    void indentSelection(bool forward);
    [[nodiscard]] QString indentText() const;

    void emitPositionRequest(const QPoint& viewportPoint, bool definition);
    void refreshExtraSelections();
    [[nodiscard]] QTextCursor cursorForRange(int startLine, int startCharacter, int endLine, int endCharacter) const;
    void applyProposal(const CompletionProposal& proposal);
    void presentDocumentation(int row);
    void hideCompletions();
    [[nodiscard]] QString completionPrefix() const;
    [[nodiscard]] QString diagnosticAt(const QTextCursor& cursor) const;

    LineNumberArea* m_lineNumberArea{nullptr};
    QPoint m_hoverPoint;
    QVector<LanguageDiagnostic> m_diagnostics;
    QList<QTextEdit::ExtraSelection> m_occurrenceSelections;
    QList<QTextEdit::ExtraSelection> m_diagnosticSelections;
    QList<QTextEdit::ExtraSelection> m_searchSelections;
    QVector<CompletionProposal> m_proposals;
    bool m_completionsIncomplete{false};
    QCompleter* m_completer{nullptr};
    QStandardItemModel* m_proposalModel{nullptr};
    const ui::Theme& m_theme;
    CodeColorScheme m_scheme;
    IndentStyle m_indentStyle{IndentStyle::Space};
    int m_indentWidth{4};
};

class LineNumberArea final : public QWidget {
  public:
    explicit LineNumberArea(CodeEditorWidget& editor);

    [[nodiscard]] QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    CodeEditorWidget& m_editor;
};

} // namespace workpane::plugins::codeeditor
