#pragma once

#include "LanguageRegistry.h"
#include "domain/Result.h"

#include <QColor>
#include <QHash>
#include <QString>
#include <QTextCharFormat>
#include <QVector>

namespace workpane::plugins::codeeditor {

// A scheme owns the surface the code is read on, because ink alone cannot be read on a background it does not control.
struct CodeColorSchemeSurface final {
    QColor background;
    QColor currentLine;
    QColor selection;
    QColor selectionText;
    QColor lineNumber;
    QColor currentLineNumber;
    QColor lineNumberBackground;
};

// The colouring of code is editor content rather than application chrome, exactly as the ANSI colours of a terminal are terminal content.
struct CodeColorScheme final {
    QString id;
    QString name;
    CodeColorSchemeSurface surface;
    QHash<HighlightRole, QTextCharFormat> formats;

    [[nodiscard]] QTextCharFormat format(HighlightRole role) const;
    [[nodiscard]] QColor color(HighlightRole role) const;
};

class CodeColorSchemeCatalog final {
  public:
    [[nodiscard]] static const QVector<CodeColorScheme>& schemes();
    // The catalog is parsed from the text of the file rather than from its path, so every rejection it declares is exercised by a test.
    [[nodiscard]] static QVector<CodeColorScheme> parse(const QByteArray& text, Result<void>& outcome);
    // The catalog is read once, so the plugin asks here whether the file it was built from was well formed.
    [[nodiscard]] static const Result<void>& catalogError();
    [[nodiscard]] static Result<void>& mutableCatalogError();
    // A scheme is resolved from an identifier the caller already validated, and an unknown one is answered rather than thrown.
    [[nodiscard]] static const CodeColorScheme* scheme(const QString& id);
    [[nodiscard]] static bool exists(const QString& id);
    [[nodiscard]] static const QString& defaultSchemeId();
};

} // namespace workpane::plugins::codeeditor
