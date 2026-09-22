#include "CodeColorScheme.h"

#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace workpane::plugins::codeeditor {

class CodeColorSchemeHelper final {
  public:
    static QByteArray readCatalog(Result<void>& outcome);
    static QColor color(const QJsonObject& entry, const QString& key, bool& valid);
    static CodeColorSchemeSurface surface(const QJsonObject& entry, bool& valid);
    static QTextCharFormat format(const QJsonObject& entry, bool& valid);
};

QByteArray CodeColorSchemeHelper::readCatalog(Result<void>& outcome) {
    QFile file(QStringLiteral(":/workpane/code-editor/assets/schemes.json"));

    if (!file.open(QIODevice::ReadOnly)) {
        outcome = Result<void>::failure({"code_editor_schemes_invalid", "The colour scheme catalog is unavailable", {}});
        return {};
    }

    return file.readAll();
}

QColor CodeColorSchemeHelper::color(const QJsonObject& entry, const QString& key, bool& valid) {
    if (!entry.value(key).isString()) {
        valid = false;
        return {};
    }

    const QColor parsed = QColor::fromString(entry.value(key).toString());

    if (!parsed.isValid()) {
        valid = false;
    }

    return parsed;
}

CodeColorSchemeSurface CodeColorSchemeHelper::surface(const QJsonObject& entry, bool& valid) {
    CodeColorSchemeSurface values;
    values.background = color(entry, QStringLiteral("background"), valid);
    values.currentLine = color(entry, QStringLiteral("currentLine"), valid);
    values.selection = color(entry, QStringLiteral("selection"), valid);
    values.selectionText = color(entry, QStringLiteral("selectionText"), valid);
    values.lineNumber = color(entry, QStringLiteral("lineNumber"), valid);
    values.currentLineNumber = color(entry, QStringLiteral("currentLineNumber"), valid);
    values.lineNumberBackground = color(entry, QStringLiteral("lineNumberBackground"), valid);

    if (entry.keys().size() != 7) {
        valid = false;
    }

    return values;
}

QTextCharFormat CodeColorSchemeHelper::format(const QJsonObject& entry, bool& valid) {
    QTextCharFormat value;
    value.setForeground(color(entry, QStringLiteral("foreground"), valid));

    for (const auto& key : entry.keys()) {
        if (key != QStringLiteral("foreground") && key != QStringLiteral("bold") && key != QStringLiteral("italic") && key != QStringLiteral("underline")) {
            valid = false;
            return value;
        }
    }

    if (entry.contains(QStringLiteral("bold"))) {
        if (!entry.value(QStringLiteral("bold")).isBool()) {
            valid = false;
            return value;
        }

        value.setFontWeight(entry.value(QStringLiteral("bold")).toBool() ? QFont::DemiBold : QFont::Normal);
    }

    if (entry.contains(QStringLiteral("italic"))) {
        if (!entry.value(QStringLiteral("italic")).isBool()) {
            valid = false;
            return value;
        }

        value.setFontItalic(entry.value(QStringLiteral("italic")).toBool());
    }

    if (entry.contains(QStringLiteral("underline"))) {
        if (!entry.value(QStringLiteral("underline")).isBool()) {
            valid = false;
            return value;
        }

        value.setFontUnderline(entry.value(QStringLiteral("underline")).toBool());
    }

    return value;
}

QVector<CodeColorScheme> CodeColorSchemeCatalog::parse(const QByteArray& text, Result<void>& outcome) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text, &error);

    if (error.error != QJsonParseError::NoError || !document.isObject() || !document.object().value(QStringLiteral("schemes")).isArray()) {
        outcome = Result<void>::failure({"code_editor_schemes_invalid", "The colour scheme catalog is not a catalog", error.errorString()});
        return {};
    }

    const QJsonObject catalog = document.object();
    QVector<CodeColorScheme> schemes;
    QSet<QString> identifiers;

    for (const auto& value : catalog.value(QStringLiteral("schemes")).toArray()) {
        const QJsonObject entry = value.toObject();
        bool valid = entry.value(QStringLiteral("id")).isString() && entry.value(QStringLiteral("name")).isString() && entry.value(QStringLiteral("surface")).isObject() && entry.value(QStringLiteral("roles")).isObject();

        if (!valid) {
            outcome = Result<void>::failure({"code_editor_schemes_invalid", "A colour scheme is not a scheme", entry.value(QStringLiteral("id")).toString()});
            return {};
        }

        CodeColorScheme scheme;
        scheme.id = entry.value(QStringLiteral("id")).toString();
        scheme.name = entry.value(QStringLiteral("name")).toString();

        if (scheme.id.isEmpty() || scheme.name.isEmpty() || identifiers.contains(scheme.id)) {
            outcome = Result<void>::failure({"code_editor_schemes_invalid", "A colour scheme repeats an identifier or declares none", scheme.id});
            return {};
        }

        identifiers.insert(scheme.id);
        scheme.surface = CodeColorSchemeHelper::surface(entry.value(QStringLiteral("surface")).toObject(), valid);

        if (!valid) {
            outcome = Result<void>::failure({"code_editor_schemes_invalid", "A colour scheme declares an invalid surface", scheme.id});
            return {};
        }

        const QJsonObject roles = entry.value(QStringLiteral("roles")).toObject();

        for (const auto& key : roles.keys()) {
            if (!roles.value(key).isObject()) {
                outcome = Result<void>::failure({"code_editor_schemes_invalid", "A colour scheme role is not a role", key});
                return {};
            }
        }

        for (const HighlightRole role : HighlightRoles::highlightRoles()) {
            const QString identifier = HighlightRoles::highlightRoleIdentifier(role);

            if (!roles.contains(identifier)) {
                outcome = Result<void>::failure({"code_editor_schemes_invalid", "A colour scheme leaves a declared role uncoloured", QStringLiteral("%1/%2").arg(scheme.id, identifier)});
                return {};
            }

            scheme.formats.insert(role, CodeColorSchemeHelper::format(roles.value(identifier).toObject(), valid));

            if (!valid) {
                outcome = Result<void>::failure({"code_editor_schemes_invalid", "A colour scheme role is invalid", QStringLiteral("%1/%2").arg(scheme.id, identifier)});
                return {};
            }
        }

        if (roles.keys().size() != HighlightRoles::highlightRoles().size()) {
            outcome = Result<void>::failure({"code_editor_schemes_invalid", "A colour scheme declares a role nobody declares", scheme.id});
            return {};
        }

        schemes.append(scheme);
    }

    if (schemes.isEmpty()) {
        outcome = Result<void>::failure({"code_editor_schemes_invalid", "The catalog declares no colour scheme", {}});
    }

    return schemes;
}

QTextCharFormat CodeColorScheme::format(HighlightRole role) const {
    return formats.value(role);
}

QColor CodeColorScheme::color(HighlightRole role) const {
    return formats.value(role).foreground().color();
}

const QVector<CodeColorScheme>& CodeColorSchemeCatalog::schemes() {
    // clang-format off
    static const QVector<CodeColorScheme> values = [] {
        Result<void>& outcome = mutableCatalogError();
        const QByteArray text = CodeColorSchemeHelper::readCatalog(outcome);
        return outcome.hasValue() ? parse(text, outcome) : QVector<CodeColorScheme>{};
    }();
    // clang-format on
    return values;
}

Result<void>& CodeColorSchemeCatalog::mutableCatalogError() {
    static Result<void> outcome = Result<void>::success();
    return outcome;
}

const Result<void>& CodeColorSchemeCatalog::catalogError() {
    // clang-format off
    static const bool built = [] { return !schemes().isEmpty(); }();
    // clang-format on
    Q_UNUSED(built);
    return mutableCatalogError();
}

const CodeColorScheme* CodeColorSchemeCatalog::scheme(const QString& id) {
    for (const auto& value : schemes()) {
        if (value.id == id) {
            return &value;
        }
    }

    return nullptr;
}

bool CodeColorSchemeCatalog::exists(const QString& id) {
    return scheme(id) != nullptr;
}

// The first declared scheme is what a first run opens on, so the catalog decides the default rather than the code that reads it.
const QString& CodeColorSchemeCatalog::defaultSchemeId() {
    static const QString value = schemes().isEmpty() ? QString{} : schemes().first().id;
    return value;
}

} // namespace workpane::plugins::codeeditor
