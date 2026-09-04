#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace workpane::plugins::codeeditor {

enum class IndentStyle { Tab, Space };

enum class LineEnding { Lf, Crlf, Cr };

enum class TextCharset { Utf8, Utf8Bom, Utf16Le, Utf16Be, Latin1 };

struct EditorConfigProperties final {
    std::optional<IndentStyle> indentStyle;
    std::optional<int> indentSize;
    std::optional<int> tabWidth;
    std::optional<LineEnding> lineEnding;
    std::optional<TextCharset> charset;
    std::optional<bool> trimTrailingWhitespace;
    std::optional<bool> insertFinalNewline;
    std::optional<int> maximumLineLength;
    QStringList unsupportedCharsets;
};

struct EditorConfigFile final {
    QString directoryPath;
    QString content;
};

class EditorConfigs final {
  public:
    [[nodiscard]] static QString textCharsetName(TextCharset charset);
    [[nodiscard]] static std::optional<TextCharset> parseTextCharset(const QString& name);
    [[nodiscard]] static const QVector<TextCharset>& textCharsets();
    [[nodiscard]] static QStringList editorConfigSearchPaths(const QString& filePath, const QString& rootPath);
    [[nodiscard]] static EditorConfigProperties resolveEditorConfig(const QString& filePath, const QVector<EditorConfigFile>& files);
    [[nodiscard]] static bool editorConfigSectionMatches(const QString& pattern, const QString& directoryPath, const QString& filePath);
    [[nodiscard]] static int resolvedIndentWidth(const EditorConfigProperties& properties);
};

} // namespace workpane::plugins::codeeditor
