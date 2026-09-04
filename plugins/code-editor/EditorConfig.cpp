#include "EditorConfig.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtGlobal>

#include <algorithm>
#include <cstdlib>
#include <optional>

namespace workpane::plugins::codeeditor {

constexpr int maximumBraceDepth = 16;
constexpr qint64 maximumRangeValues = 4096;

class EditorConfigHelper final {
  public:
    static QString escapedLiteral(QChar character);
    static QString translateSegment(const QString& pattern, qsizetype& index, bool stopOnAlternation, int depth);
    static QString translateBraces(const QString& pattern, qsizetype& index, int depth);
    static bool declaresDirectory(const QString& pattern);
    static QRegularExpression sectionExpression(const QString& pattern);
    static QString significantLine(const QString& line);
    static std::optional<bool> parseBoolean(const QString& value);
    static std::optional<int> parsePositiveInteger(const QString& value);
    static void applyIndentStyle(EditorConfigProperties& properties, const QString& value);
    static void applyLineEnding(EditorConfigProperties& properties, const QString& value);
    static void applyCharset(EditorConfigProperties& properties, const QString& value);
    static void clearProperty(EditorConfigProperties& properties, const QString& key, bool& indentSizeFollowsTab);
    static void applyProperty(EditorConfigProperties& properties, const QString& key, const QString& value, bool& indentSizeFollowsTab);
    static bool declaresRoot(const QString& content);
    static void mergeFile(EditorConfigProperties& properties, const EditorConfigFile& file, const QString& filePath, bool& indentSizeFollowsTab);
};

QString EditorConfigHelper::escapedLiteral(QChar character) {
    return QRegularExpression::escape(QString(character));
}

QString EditorConfigHelper::translateSegment(const QString& pattern, qsizetype& index, bool stopOnAlternation, int depth) {
    QString expression;

    while (index < pattern.size()) {
        const QChar character = pattern.at(index);
        if (character == QLatin1Char('}') || (stopOnAlternation && character == QLatin1Char(','))) {
            return expression;
        }

        ++index;
        if (character == QLatin1Char('{')) {
            expression += EditorConfigHelper::translateBraces(pattern, index, depth);
            continue;
        }
        if (character == QLatin1Char('\\') && index < pattern.size()) {
            expression += EditorConfigHelper::escapedLiteral(pattern.at(index));
            ++index;
            continue;
        }
        if (character == QLatin1Char('?')) {
            expression += QStringLiteral("[^/]");
            continue;
        }
        if (character == QLatin1Char('[')) {
            const bool negated = index < pattern.size() && (pattern.at(index) == QLatin1Char('!') || pattern.at(index) == QLatin1Char('^'));
            qsizetype scan = index + (negated ? 1 : 0);
            QString characterClass;
            bool closed = false;
            bool offersSeparator = false;
            while (scan < pattern.size()) {
                const QChar member = pattern.at(scan);
                if (member == QLatin1Char('\\') && scan + 1 < pattern.size()) {
                    characterClass += EditorConfigHelper::escapedLiteral(pattern.at(scan + 1));
                    scan += 2;
                    continue;
                }
                if (member == QLatin1Char(']')) {
                    closed = true;
                    ++scan;
                    break;
                }
                offersSeparator = offersSeparator || member == QLatin1Char('/');
                characterClass += member == QLatin1Char('^') ? QStringLiteral("\\^") : QString(member);
                ++scan;
            }
            // A class that never closes, or one offering a path separator, is the literal text it spells.
            if (!closed || offersSeparator) {
                expression += EditorConfigHelper::escapedLiteral(QLatin1Char('['));
                continue;
            }

            index = scan;
            expression += QStringLiteral("[%1%2]").arg(negated ? QStringLiteral("^") : QString{}, characterClass);
            continue;
        }
        // A double star between separators also stands for no directory at all, so one separator is what is left of it.
        if (character == QLatin1Char('/') && pattern.mid(index, 3) == QStringLiteral("**/")) {
            index += 3;
            expression += QStringLiteral("(?:/|/.*/)");
            continue;
        }
        if (character == QLatin1Char('*')) {
            if (index < pattern.size() && pattern.at(index) == QLatin1Char('*')) {
                ++index;
                expression += QStringLiteral(".*");
                continue;
            }
            expression += QStringLiteral("[^/]*");
            continue;
        }
        expression += EditorConfigHelper::escapedLiteral(character);
    }

    return expression;
}

// Translates one EditorConfig brace expression into a regular expression alternation.
QString EditorConfigHelper::translateBraces(const QString& pattern, qsizetype& index, int depth) {
    const qsizetype start = index;

    // A brace that fails to read rewinds and is spelled literally, so a run of them would re-read the tail once per brace.
    if (depth >= maximumBraceDepth) {
        return EditorConfigHelper::escapedLiteral(QLatin1Char('{'));
    }

    QStringList alternatives;
    QString current = EditorConfigHelper::translateSegment(pattern, index, true, depth + 1);

    while (index < pattern.size() && pattern.at(index) == QLatin1Char(',')) {
        ++index;
        alternatives.append(current);
        current = EditorConfigHelper::translateSegment(pattern, index, true, depth + 1);
    }

    // A brace that never closes is the literal text it spells, kept as it was already read so no caller reads that tail again.
    if (index >= pattern.size() || pattern.at(index) != QLatin1Char('}')) {
        alternatives.append(current);
        return EditorConfigHelper::escapedLiteral(QLatin1Char('{')) + alternatives.join(EditorConfigHelper::escapedLiteral(QLatin1Char(',')));
    }

    ++index;

    if (alternatives.isEmpty()) {
        const QString range = pattern.mid(start, index - start - 1);
        const qsizetype separator = range.indexOf(QStringLiteral(".."));
        bool lowValid = false;
        bool highValid = false;
        const int low = separator > 0 ? range.first(separator).toInt(&lowValid) : 0;
        const int high = separator > 0 ? range.mid(separator + 2).toInt(&highValid) : 0;
        if (lowValid && highValid && std::abs(static_cast<qint64>(std::max(low, high)) - static_cast<qint64>(std::min(low, high))) < maximumRangeValues) {
            QStringList numbers;
            for (int value = std::min(low, high); value <= std::max(low, high); ++value) {
                numbers.append(QRegularExpression::escape(QString::number(value)));
            }
            return QStringLiteral("(?:%1)").arg(numbers.join(QLatin1Char('|')));
        }
        return EditorConfigHelper::escapedLiteral(QLatin1Char('{')) + current + EditorConfigHelper::escapedLiteral(QLatin1Char('}'));
    }

    alternatives.append(current);
    return QStringLiteral("(?:%1)").arg(alternatives.join(QLatin1Char('|')));
}

// A separator inside a character class is one of the characters that class offers rather than a level of the path.
bool EditorConfigHelper::declaresDirectory(const QString& pattern) {
    bool insideClass = false;

    for (qsizetype index = 0; index < pattern.size(); ++index) {
        const QChar character = pattern.at(index);
        if (character == QLatin1Char('\\')) {
            ++index;
            continue;
        }
        if (character == QLatin1Char('[')) {
            insideClass = true;
            continue;
        }
        if (character == QLatin1Char(']')) {
            insideClass = false;
            continue;
        }
        if (character == QLatin1Char('/') && !insideClass) {
            return true;
        }
    }

    return false;
}

QRegularExpression EditorConfigHelper::sectionExpression(const QString& pattern) {
    QString normalized = pattern;
    const bool anyDirectory = !EditorConfigHelper::declaresDirectory(normalized);

    if (normalized.startsWith(QLatin1Char('/'))) {
        normalized = normalized.mid(1);
    }

    qsizetype index = 0;
    const QString expression = EditorConfigHelper::translateSegment(normalized, index, false, 0);
    return QRegularExpression(QStringLiteral("\\A%1(?:%2)\\z").arg(anyDirectory ? QStringLiteral("(?:.*/)?") : QString{}, expression));
}

// A comment is a whole line, so a hash or a semicolon anywhere else is the literal text of a value or of a section name.
QString EditorConfigHelper::significantLine(const QString& line) {
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QLatin1Char('#')) || trimmed.startsWith(QLatin1Char(';')) ? QString{} : trimmed;
}

std::optional<bool> EditorConfigHelper::parseBoolean(const QString& value) {
    if (value == QStringLiteral("true")) {
        return true;
    }
    if (value == QStringLiteral("false")) {
        return false;
    }

    return std::nullopt;
}

std::optional<int> EditorConfigHelper::parsePositiveInteger(const QString& value) {
    bool valid = false;
    const int parsed = value.toInt(&valid);

    if (!valid || parsed <= 0) {
        return std::nullopt;
    }

    return parsed;
}

void EditorConfigHelper::applyIndentStyle(EditorConfigProperties& properties, const QString& value) {
    if (value == QStringLiteral("tab")) {
        properties.indentStyle = IndentStyle::Tab;
        return;
    }

    if (value == QStringLiteral("space")) {
        properties.indentStyle = IndentStyle::Space;
    }
}

void EditorConfigHelper::applyLineEnding(EditorConfigProperties& properties, const QString& value) {
    if (value == QStringLiteral("lf")) {
        properties.lineEnding = LineEnding::Lf;
        return;
    }

    if (value == QStringLiteral("crlf")) {
        properties.lineEnding = LineEnding::Crlf;
        return;
    }

    if (value == QStringLiteral("cr")) {
        properties.lineEnding = LineEnding::Cr;
    }
}

void EditorConfigHelper::applyCharset(EditorConfigProperties& properties, const QString& value) {
    if (const auto charset = EditorConfigs::parseTextCharset(value); charset.has_value()) {
        properties.charset = charset;
        return;
    }

    properties.charset = std::nullopt;
    properties.unsupportedCharsets.removeAll(value);
    properties.unsupportedCharsets.append(value);
}

// A value of unset undoes what a file above declared, which is what the specification has it for.
void EditorConfigHelper::clearProperty(EditorConfigProperties& properties, const QString& key, bool& indentSizeFollowsTab) {
    if (key == QStringLiteral("indent_style")) {
        properties.indentStyle = std::nullopt;
        return;
    }

    if (key == QStringLiteral("indent_size")) {
        properties.indentSize = std::nullopt;
        indentSizeFollowsTab = false;
        return;
    }

    if (key == QStringLiteral("tab_width")) {
        properties.tabWidth = std::nullopt;
        return;
    }

    if (key == QStringLiteral("end_of_line")) {
        properties.lineEnding = std::nullopt;
        return;
    }

    if (key == QStringLiteral("charset")) {
        properties.charset = std::nullopt;
        properties.unsupportedCharsets.clear();
        return;
    }

    if (key == QStringLiteral("trim_trailing_whitespace")) {
        properties.trimTrailingWhitespace = std::nullopt;
        return;
    }

    if (key == QStringLiteral("insert_final_newline")) {
        properties.insertFinalNewline = std::nullopt;
        return;
    }

    if (key == QStringLiteral("max_line_length")) {
        properties.maximumLineLength = std::nullopt;
    }
}

void EditorConfigHelper::applyProperty(EditorConfigProperties& properties, const QString& key, const QString& value, bool& indentSizeFollowsTab) {
    if (value == QStringLiteral("unset")) {
        EditorConfigHelper::clearProperty(properties, key, indentSizeFollowsTab);
        return;
    }

    if (key == QStringLiteral("indent_style")) {
        EditorConfigHelper::applyIndentStyle(properties, value);
        return;
    }

    // An indent size that follows the tab width is resolved once every file has been read, so the two can be written in either order.
    if (key == QStringLiteral("indent_size")) {
        indentSizeFollowsTab = value == QStringLiteral("tab");
        properties.indentSize = indentSizeFollowsTab ? std::nullopt : EditorConfigHelper::parsePositiveInteger(value);
        return;
    }

    if (key == QStringLiteral("tab_width")) {
        properties.tabWidth = EditorConfigHelper::parsePositiveInteger(value);
        return;
    }

    if (key == QStringLiteral("end_of_line")) {
        EditorConfigHelper::applyLineEnding(properties, value);
        return;
    }

    if (key == QStringLiteral("charset")) {
        EditorConfigHelper::applyCharset(properties, value);
        return;
    }

    if (key == QStringLiteral("trim_trailing_whitespace")) {
        properties.trimTrailingWhitespace = EditorConfigHelper::parseBoolean(value);
        return;
    }

    if (key == QStringLiteral("insert_final_newline")) {
        properties.insertFinalNewline = EditorConfigHelper::parseBoolean(value);
        return;
    }

    if (key == QStringLiteral("max_line_length")) {
        properties.maximumLineLength = value == QStringLiteral("off") ? std::nullopt : EditorConfigHelper::parsePositiveInteger(value);
    }
}

bool EditorConfigHelper::declaresRoot(const QString& content) {
    const QStringList lines = content.split(QLatin1Char('\n'));

    for (const auto& rawLine : lines) {
        const QString line = EditorConfigHelper::significantLine(rawLine);
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            return false;
        }
        const qsizetype separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }
        if (line.first(separator).trimmed().toLower() == QStringLiteral("root")) {
            return line.mid(separator + 1).trimmed().toLower() == QStringLiteral("true");
        }
    }

    return false;
}

void EditorConfigHelper::mergeFile(EditorConfigProperties& properties, const EditorConfigFile& file, const QString& filePath, bool& indentSizeFollowsTab) {
    const QStringList lines = file.content.split(QLatin1Char('\n'));
    bool sectionMatches = false;
    bool insideSection = false;

    for (const auto& rawLine : lines) {
        const QString line = EditorConfigHelper::significantLine(rawLine);
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            insideSection = true;
            sectionMatches = EditorConfigs::editorConfigSectionMatches(line.mid(1, line.size() - 2), file.directoryPath, filePath);
            continue;
        }
        if (!insideSection || !sectionMatches) {
            continue;
        }
        const qsizetype separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }
        EditorConfigHelper::applyProperty(properties, line.first(separator).trimmed().toLower(), line.mid(separator + 1).trimmed().toLower(), indentSizeFollowsTab);
    }
}

QStringList EditorConfigs::editorConfigSearchPaths(const QString& filePath, const QString& rootPath) {
    const QString normalizedRoot = QDir::cleanPath(rootPath);
    QString directory = QFileInfo(QDir::cleanPath(filePath)).absolutePath();
    QStringList paths;

    while (directory == normalizedRoot || directory.startsWith(normalizedRoot + QLatin1Char('/'))) {
        paths.append(QDir(directory).filePath(QStringLiteral(".editorconfig")));
        if (directory == normalizedRoot) {
            break;
        }
        const QString parent = QFileInfo(directory).absolutePath();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }

    return paths;
}

bool EditorConfigs::editorConfigSectionMatches(const QString& pattern, const QString& directoryPath, const QString& filePath) {
    const QString normalizedDirectory = QDir::cleanPath(directoryPath);
    const QString normalizedFile = QDir::cleanPath(filePath);

    if (!normalizedFile.startsWith(normalizedDirectory + QLatin1Char('/'))) {
        return false;
    }

    const QString relativePath = normalizedFile.mid(normalizedDirectory.size() + 1);
    return EditorConfigHelper::sectionExpression(pattern).match(relativePath).hasMatch();
}

EditorConfigProperties EditorConfigs::resolveEditorConfig(const QString& filePath, const QVector<EditorConfigFile>& files) {
    qsizetype boundary = files.size();

    for (qsizetype index = 0; index < files.size(); ++index) {
        if (EditorConfigHelper::declaresRoot(files.at(index).content)) {
            boundary = index + 1;
            break;
        }
    }

    EditorConfigProperties properties;
    bool indentSizeFollowsTab = false;

    for (qsizetype index = boundary - 1; index >= 0; --index) {
        EditorConfigHelper::mergeFile(properties, files.at(index), filePath, indentSizeFollowsTab);
    }

    if (indentSizeFollowsTab) {
        properties.indentSize = properties.tabWidth;
    }

    return properties;
}

// The names are the ones EditorConfig declares, so a stored value and a declared one read the same.
QString EditorConfigs::textCharsetName(TextCharset charset) {
    switch (charset) {
    case TextCharset::Utf8:
        return QStringLiteral("utf-8");
    case TextCharset::Utf8Bom:
        return QStringLiteral("utf-8-bom");
    case TextCharset::Utf16Le:
        return QStringLiteral("utf-16le");
    case TextCharset::Utf16Be:
        return QStringLiteral("utf-16be");
    case TextCharset::Latin1:
        return QStringLiteral("latin1");
    }

    return QStringLiteral("utf-8");
}

std::optional<TextCharset> EditorConfigs::parseTextCharset(const QString& name) {
    for (const auto charset : EditorConfigs::textCharsets()) {
        if (EditorConfigs::textCharsetName(charset) == name) {
            return charset;
        }
    }

    return std::nullopt;
}

const QVector<TextCharset>& EditorConfigs::textCharsets() {
    static const QVector<TextCharset> charsets{TextCharset::Utf8, TextCharset::Utf8Bom, TextCharset::Utf16Le, TextCharset::Utf16Be, TextCharset::Latin1};
    return charsets;
}

int EditorConfigs::resolvedIndentWidth(const EditorConfigProperties& properties) {
    if (properties.indentStyle.value_or(IndentStyle::Space) == IndentStyle::Tab) {
        return properties.tabWidth.value_or(properties.indentSize.value_or(4));
    }

    return properties.indentSize.value_or(properties.tabWidth.value_or(4));
}

} // namespace workpane::plugins::codeeditor
