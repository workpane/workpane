#pragma once

#include "EditorConfig.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace workpane::plugins::codeeditor {

struct OpenDocumentState final {
    QString path;
    int position{0};
    int cursorPosition{0};
    bool active{false};
};

inline constexpr int defaultEditorFontSize = 10;

struct CodeEditorFont final {
    QString family;
    int size{defaultEditorFontSize};
};

struct CodeEditorSettings final {
    bool wordWrap{false};
    bool languageServersEnabled{true};
    QString fontFamily;
    int fontSize{defaultEditorFontSize};
    // A file carrying no mark and spelling no valid UTF-8 is read in this one, and Latin-1 is the only encoding that returns every byte it was given.
    TextCharset defaultCharset{TextCharset::Latin1};
    // The colouring of code is editor content, so it is selected here and never follows the application theme.
    QString colorSchemeId;
};

struct CodeWorkspaceState final {
    QString id;
    QString rootPath;
    int position{0};
    bool active{false};
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
    QVector<OpenDocumentState> documents;
};

class StatePaths final {
  public:
    // A Windows path names the same file with another case and even with another drive letter case, so the comparison follows the filesystem instead of the bytes.
    [[nodiscard]] static inline bool samePath(const QString& first, const QString& second) {
#ifdef Q_OS_WIN
        return first.compare(second, Qt::CaseInsensitive) == 0;
#else
        return first == second;
#endif
    }
};

} // namespace workpane::plugins::codeeditor
