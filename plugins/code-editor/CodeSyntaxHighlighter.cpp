#include "CodeSyntaxHighlighter.h"

#include <QRegularExpression>
#include <QTextBlock>

#include <utility>

namespace workpane::plugins::codeeditor {

class CodeSyntaxHighlighterHelper final {
  public:
    static QString keywordPattern(const QStringList& keywords);
    static QStringList keywordsIn(const QStringList& keywords, const QStringList& set);
    static QStringList keywordsOutside(const QStringList& keywords, const QStringList& first, const QStringList& second);
};

QString CodeSyntaxHighlighterHelper::keywordPattern(const QStringList& keywords) {
    QStringList escaped;
    escaped.reserve(keywords.size());

    for (const auto& keyword : keywords) {
        escaped.append(QRegularExpression::escape(keyword));
    }

    return QStringLiteral("\\b(?:%1)\\b").arg(escaped.join(QLatin1Char('|')));
}

QStringList CodeSyntaxHighlighterHelper::keywordsIn(const QStringList& keywords, const QStringList& set) {
    QStringList values;

    for (const auto& keyword : keywords) {
        if (set.contains(keyword)) {
            values.append(keyword);
        }
    }

    return values;
}

QStringList CodeSyntaxHighlighterHelper::keywordsOutside(const QStringList& keywords, const QStringList& first, const QStringList& second) {
    QStringList values;

    for (const auto& keyword : keywords) {
        if (!first.contains(keyword) && !second.contains(keyword)) {
            values.append(keyword);
        }
    }

    return values;
}

// The order a rule is added in decides which one wins, so a keyword beats the shape that only guessed at it and a string beats them both.
CodeSyntaxHighlighter::CodeSyntaxHighlighter(QTextDocument* document, LanguageDefinition definition, const CodeColorScheme& scheme) : QSyntaxHighlighter(document), m_definition(std::move(definition)) {
    m_commentFormat = scheme.format(HighlightRole::Comment);

    if (m_definition.sharedPatterns) {
        for (const auto& pattern : LanguageRegistry::patternsBeforeKeywords()) {
            m_rules.append({QRegularExpression(pattern.pattern), scheme.format(pattern.role)});
        }
    }

    const QStringList controlFlow = CodeSyntaxHighlighterHelper::keywordsIn(m_definition.keywords, LanguageRegistry::controlFlowKeywords());
    const QStringList primitiveTypes = CodeSyntaxHighlighterHelper::keywordsIn(m_definition.keywords, LanguageRegistry::primitiveTypeKeywords());
    const QStringList plain = CodeSyntaxHighlighterHelper::keywordsOutside(m_definition.keywords, LanguageRegistry::controlFlowKeywords(), LanguageRegistry::primitiveTypeKeywords());
    const QVector<QPair<QStringList, HighlightRole>> keywordGroups{{plain, HighlightRole::Keyword}, {controlFlow, HighlightRole::ControlFlow}, {primitiveTypes, HighlightRole::PrimitiveType}};

    for (const auto& group : keywordGroups) {
        if (!group.first.isEmpty()) {
            m_rules.append({QRegularExpression(CodeSyntaxHighlighterHelper::keywordPattern(group.first)), scheme.format(group.second)});
        }
    }

    if (m_definition.sharedPatterns) {
        for (const auto& pattern : LanguageRegistry::patternsAfterKeywords()) {
            m_rules.append({QRegularExpression(pattern.pattern), scheme.format(pattern.role)});
        }
    }

    for (const auto& pattern : m_definition.patterns) {
        m_rules.append({QRegularExpression(pattern.pattern), scheme.format(pattern.role)});
    }

    const QMap<QString, HighlightRole>& roles = LanguageRegistry::semanticRoles();

    for (auto entry = roles.constBegin(); entry != roles.constEnd(); ++entry) {
        m_semanticFormats.insert(entry.key(), scheme.format(entry.value()));
    }

    if (!m_definition.lineComment.isEmpty()) {
        m_rules.append({QRegularExpression(QStringLiteral("%1.*$").arg(QRegularExpression::escape(m_definition.lineComment))), m_commentFormat});
    }
}

// The server knows what a name really is, so its token wins over the pattern that only guessed from the shape of the text.
// The tokens arrive already decoded and grouped, so this only decides which lines changed.
// Repainting the whole document on every answer costs the file, so nothing else is invalidated.
void CodeSyntaxHighlighter::setSemanticTokens(const SemanticTokenSet& tokens) {
    QSet<int> changed;

    for (auto entry = tokens.constBegin(); entry != tokens.constEnd(); ++entry) {
        if (m_semanticTokens.value(entry.key()) != entry.value()) {
            changed.insert(entry.key());
        }
    }

    for (auto entry = m_semanticTokens.constBegin(); entry != m_semanticTokens.constEnd(); ++entry) {
        if (!tokens.contains(entry.key())) {
            changed.insert(entry.key());
        }
    }

    m_semanticTokens = tokens;

    if (changed.isEmpty()) {
        return;
    }

    // Invalidating one line at a time stops paying off once most of them changed, which is what the first answer for a file does.
    if (changed.size() * LanguageRegistry::limits().partialRepaintDivisor >= document()->blockCount()) {
        rehighlight();
        return;
    }

    for (const int line : changed) {
        const QTextBlock block = document()->findBlockByNumber(line);

        if (block.isValid()) {
            rehighlightBlock(block);
        }
    }
}

// A single line longer than the declared bound is generated content, and running every pattern over it costs more than the colors are worth.
void CodeSyntaxHighlighter::highlightBlock(const QString& text) {
    if (text.size() <= LanguageRegistry::limits().maximumHighlightedLineLength) {
        int applied = 0;

        for (const auto& rule : m_rules) {
            applied += applyRule(text, rule);
        }

        applyBlockComments(text);

        // A line decorated past this bound costs more to lay out on every edit than the colours are worth, so it keeps its text and loses them.
        if (applied > LanguageRegistry::limits().maximumHighlightedMatchesPerLine) {
            setFormat(0, static_cast<int>(text.size()), QTextCharFormat());
        }
    }

    for (const auto& token : m_semanticTokens.value(currentBlock().blockNumber())) {
        if (token.startCharacter >= 0 && token.startCharacter + token.length <= text.size()) {
            QTextCharFormat format = m_semanticFormats.value(token.type);
            format.setFontStrikeOut(token.deprecated);
            format.setFontItalic(token.readOnly);
            setFormat(token.startCharacter, token.length, format);
        }
    }
}

int CodeSyntaxHighlighter::applyRule(const QString& text, const Rule& rule) {
    auto match = rule.expression.globalMatch(text);
    int applied = 0;

    while (match.hasNext()) {
        const auto current = match.next();
        setFormat(static_cast<int>(current.capturedStart()), static_cast<int>(current.capturedLength()), rule.format);
        ++applied;
    }

    return applied;
}

void CodeSyntaxHighlighter::applyBlockComments(const QString& text) {
    if (m_definition.blockCommentStart.isEmpty() || m_definition.blockCommentEnd.isEmpty()) {
        return;
    }

    setCurrentBlockState(0);
    qsizetype start = previousBlockState() == 1 ? 0 : text.indexOf(m_definition.blockCommentStart);

    while (start >= 0) {
        const qsizetype end = text.indexOf(m_definition.blockCommentEnd, start + m_definition.blockCommentStart.size());
        if (end < 0) {
            setCurrentBlockState(1);
            setFormat(static_cast<int>(start), static_cast<int>(text.size() - start), m_commentFormat);
            return;
        }
        const qsizetype length = end - start + m_definition.blockCommentEnd.size();
        setFormat(static_cast<int>(start), static_cast<int>(length), m_commentFormat);
        start = text.indexOf(m_definition.blockCommentStart, start + length);
    }
}

} // namespace workpane::plugins::codeeditor
