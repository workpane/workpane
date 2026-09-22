#pragma once

#include "CodeColorScheme.h"
#include "LanguageRegistry.h"
#include "LanguageServerClient.h"

#include <QHash>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace workpane::plugins::codeeditor {

class CodeSyntaxHighlighter final : public QSyntaxHighlighter {
    Q_OBJECT

  public:
    CodeSyntaxHighlighter(QTextDocument* document, LanguageDefinition definition, const CodeColorScheme& scheme);

    void setSemanticTokens(const SemanticTokenSet& tokens);

  protected:
    void highlightBlock(const QString& text) override;

  private:
    struct Rule final {
        QRegularExpression expression;
        QTextCharFormat format;
    };

    [[nodiscard]] int applyRule(const QString& text, const Rule& rule);
    void applyBlockComments(const QString& text);

    LanguageDefinition m_definition;
    QVector<Rule> m_rules;
    QTextCharFormat m_commentFormat;
    QHash<QString, QTextCharFormat> m_semanticFormats;
    SemanticTokenSet m_semanticTokens;
};

} // namespace workpane::plugins::codeeditor
