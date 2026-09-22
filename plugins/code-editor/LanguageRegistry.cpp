#include "LanguageRegistry.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <optional>

namespace workpane::plugins::codeeditor {

class LanguageRegistryHelper final {
  public:
    static LanguageDefinition plainText();
    static void reject(Result<void>& outcome, const QString& message, const QString& detail);
    static QVector<LanguageDefinition> createLanguages(const QJsonObject& catalog, Result<void>& outcome);
    static QVector<LanguageServerDefinition> createLanguageServers(const QJsonObject& catalog, const QVector<LanguageDefinition>& languages, Result<void>& outcome);
    static QVector<HighlightPattern> sharedPatterns(const QJsonObject& catalog, const QString& key, Result<void>& outcome);
    static QStringList sharedKeywords(const QJsonObject& catalog, const QString& key, Result<void>& outcome);
    static QMap<QString, HighlightRole> createSemanticRoles(const QJsonObject& catalog, Result<void>& outcome);
    static const QVector<QPair<QString, HighlightRole>>& roleTable();
    static const LanguageCatalog& parsedCatalog();
    static EditorLimits createLimits(const QJsonObject& catalog, Result<void>& outcome);
    static QStringList textList(const QJsonObject& entry, const QString& key, bool& valid);
    static std::optional<HighlightRole> roleFromIdentifier(const QString& identifier);
    static QVector<HighlightPattern> patternList(const QJsonArray& entries, bool& valid);
};

LanguageDefinition LanguageRegistryHelper::plainText() {
    return {QStringLiteral("plaintext"), QStringLiteral("Plain Text"), {}, {}, {}, {}, {}, {}, {}, false};
}

// The first reason a catalog is refused is the one the reader hits first, so a later step does not write over it.
void LanguageRegistryHelper::reject(Result<void>& outcome, const QString& message, const QString& detail) {
    if (outcome.hasValue()) {
        outcome = Result<void>::failure({"code_editor_catalog_invalid", message, detail});
    }
}

QVector<HighlightPattern> LanguageRegistryHelper::sharedPatterns(const QJsonObject& catalog, const QString& key, Result<void>& outcome) {
    bool valid = true;
    QVector<HighlightPattern> values = patternList(catalog.value(QStringLiteral("highlighting")).toObject().value(key).toArray(), valid);

    if (!valid || values.isEmpty()) {
        reject(outcome, QStringLiteral("The catalog highlighting patterns are invalid"), key);
        return {};
    }

    return values;
}

QStringList LanguageRegistryHelper::sharedKeywords(const QJsonObject& catalog, const QString& key, Result<void>& outcome) {
    bool valid = true;
    const QStringList declared = textList(catalog.value(QStringLiteral("highlighting")).toObject(), key, valid);

    if (!valid || declared.isEmpty()) {
        reject(outcome, QStringLiteral("The catalog highlighting keywords are invalid"), key);
        return {};
    }

    return declared;
}

QStringList LanguageRegistryHelper::textList(const QJsonObject& entry, const QString& key, bool& valid) {
    if (!entry.contains(key)) {
        return {};
    }

    if (!entry.value(key).isArray()) {
        valid = false;
        return {};
    }

    QStringList values;

    for (const auto& value : entry.value(key).toArray()) {
        if (!value.isString() || value.toString().isEmpty()) {
            valid = false;
            return {};
        }
        values.append(value.toString());
    }

    return values;
}

// The complete role set is answered here, so a role is added in one place and every reader of it follows.
const QVector<QPair<QString, HighlightRole>>& LanguageRegistryHelper::roleTable() {
    // clang-format off
    static const QVector<QPair<QString, HighlightRole>> table{{QStringLiteral("text"), HighlightRole::Text}, {QStringLiteral("keyword"), HighlightRole::Keyword}, {QStringLiteral("controlFlow"), HighlightRole::ControlFlow}, {QStringLiteral("primitiveType"), HighlightRole::PrimitiveType}, {QStringLiteral("type"), HighlightRole::Type}, {QStringLiteral("namespace"), HighlightRole::Namespace}, {QStringLiteral("enumeration"), HighlightRole::Enumeration}, {QStringLiteral("constant"), HighlightRole::Constant}, {QStringLiteral("function"), HighlightRole::Function}, {QStringLiteral("method"), HighlightRole::Method}, {QStringLiteral("macro"), HighlightRole::Macro}, {QStringLiteral("parameter"), HighlightRole::Parameter}, {QStringLiteral("variable"), HighlightRole::Variable}, {QStringLiteral("property"), HighlightRole::Property}, {QStringLiteral("number"), HighlightRole::Number}, {QStringLiteral("string"), HighlightRole::String}, {QStringLiteral("regexp"), HighlightRole::Regexp}, {QStringLiteral("comment"), HighlightRole::Comment}, {QStringLiteral("operator"), HighlightRole::Operator}, {QStringLiteral("preprocessor"), HighlightRole::Preprocessor}, {QStringLiteral("label"), HighlightRole::Label}, {QStringLiteral("decorator"), HighlightRole::Decorator}, {QStringLiteral("attribute"), HighlightRole::Attribute}, {QStringLiteral("heading"), HighlightRole::Heading}, {QStringLiteral("emphasis"), HighlightRole::Emphasis}, {QStringLiteral("strong"), HighlightRole::Strong}, {QStringLiteral("link"), HighlightRole::Link}, {QStringLiteral("markup"), HighlightRole::Markup}, {QStringLiteral("codeSpan"), HighlightRole::CodeSpan}};
    // clang-format on
    return table;
}

const QVector<HighlightRole>& HighlightRoles::highlightRoles() {
    // clang-format off
    static const QVector<HighlightRole> values = [] {
        QVector<HighlightRole> roles;
        for (const auto& entry : LanguageRegistryHelper::roleTable()) {
            roles.append(entry.second);
        }
        return roles;
    }();
    // clang-format on
    return values;
}

QString HighlightRoles::highlightRoleIdentifier(HighlightRole role) {
    for (const auto& entry : LanguageRegistryHelper::roleTable()) {
        if (entry.second == role) {
            return entry.first;
        }
    }

    return {};
}

std::optional<HighlightRole> LanguageRegistryHelper::roleFromIdentifier(const QString& identifier) {
    for (const auto& entry : LanguageRegistryHelper::roleTable()) {
        if (entry.first == identifier) {
            return entry.second;
        }
    }

    return std::nullopt;
}

QVector<HighlightPattern> LanguageRegistryHelper::patternList(const QJsonArray& entries, bool& valid) {
    QVector<HighlightPattern> patterns;

    for (const auto& value : entries) {
        const QJsonObject entry = value.toObject();
        const auto role = roleFromIdentifier(entry.value(QStringLiteral("role")).toString());
        const QString expression = entry.value(QStringLiteral("pattern")).toString();
        if (!role.has_value() || expression.isEmpty() || !QRegularExpression(expression).isValid()) {
            valid = false;
            return {};
        }
        patterns.append({expression, *role});
    }

    return patterns;
}

QVector<LanguageDefinition> LanguageRegistryHelper::createLanguages(const QJsonObject& catalog, Result<void>& outcome) {
    QVector<LanguageDefinition> languages;
    QStringList claimed;

    for (const auto& value : catalog.value(QStringLiteral("languages")).toArray()) {
        const QJsonObject entry = value.toObject();
        bool valid = entry.value(QStringLiteral("id")).isString() && entry.value(QStringLiteral("name")).isString();
        LanguageDefinition language;
        language.id = entry.value(QStringLiteral("id")).toString();
        language.name = entry.value(QStringLiteral("name")).toString();
        language.extensions = textList(entry, QStringLiteral("extensions"), valid);
        language.patterns = patternList(entry.value(QStringLiteral("patterns")).toArray(), valid);
        language.fileNames = textList(entry, QStringLiteral("fileNames"), valid);
        language.lineComment = entry.value(QStringLiteral("lineComment")).toString();
        language.blockCommentStart = entry.value(QStringLiteral("blockCommentStart")).toString();
        language.blockCommentEnd = entry.value(QStringLiteral("blockCommentEnd")).toString();
        if (entry.contains(QStringLiteral("sharedPatterns"))) {
            valid = valid && entry.value(QStringLiteral("sharedPatterns")).isBool();
            language.sharedPatterns = entry.value(QStringLiteral("sharedPatterns")).toBool(true);
        }

        if (entry.contains(QStringLiteral("keywords"))) {
            valid = valid && entry.value(QStringLiteral("keywords")).isString();
            language.keywords = entry.value(QStringLiteral("keywords")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
        if (!valid || language.id.isEmpty() || language.name.isEmpty() || language.blockCommentStart.isEmpty() != language.blockCommentEnd.isEmpty()) {
            reject(outcome, QStringLiteral("A catalog language is invalid"), language.id);
            return {plainText()};
        }

        // The first language claiming an extension is the one that answers for it, so a second claim would never be reached.
        for (const auto& extension : language.extensions) {
            if (claimed.contains(extension, Qt::CaseInsensitive)) {
                reject(outcome, QStringLiteral("Two catalog languages claim one extension"), extension);
                return {plainText()};
            }
            claimed.append(extension);
        }
        languages.append(language);
    }

    if (languages.isEmpty() || languages.last().id != QStringLiteral("plaintext")) {
        reject(outcome, QStringLiteral("The language catalog does not end with plain text"), {});
        return {plainText()};
    }

    return languages;
}

QVector<LanguageServerDefinition> LanguageRegistryHelper::createLanguageServers(const QJsonObject& catalog, const QVector<LanguageDefinition>& languages, Result<void>& outcome) {
    QVector<LanguageServerDefinition> servers;

    for (const auto& value : catalog.value(QStringLiteral("servers")).toArray()) {
        const QJsonObject entry = value.toObject();
        LanguageServerDefinition definition;
        definition.languageId = entry.value(QStringLiteral("language")).toString();
        bool valid = !definition.languageId.isEmpty() && entry.value(QStringLiteral("candidates")).isArray();
        for (const auto& candidateValue : entry.value(QStringLiteral("candidates")).toArray()) {
            const QJsonObject candidateEntry = candidateValue.toObject();
            LanguageServerCandidate candidate;
            candidate.executableName = candidateEntry.value(QStringLiteral("executable")).toString();
            candidate.arguments = textList(candidateEntry, QStringLiteral("arguments"), valid);
            valid = valid && !candidate.executableName.isEmpty();
            definition.candidates.append(candidate);
        }
        // clang-format off
        const auto declaresLanguage = [&languages, &definition]() {
            for (const auto& language : languages) {
                if (language.id == definition.languageId) {
                    return true;
                }
            }

            return false;
        };
        // clang-format on

        if (!valid || definition.candidates.isEmpty() || !declaresLanguage()) {
            reject(outcome, QStringLiteral("A catalog language server is invalid"), definition.languageId);
            return {};
        }
        servers.append(definition);
    }

    return servers;
}

Result<void>& LanguageRegistry::mutableCatalogError() {
    static Result<void> outcome = Result<void>::success();
    return outcome;
}

// Both lists are read from the same file, so asking for the outcome builds whichever of them has not been built yet.
const Result<void>& LanguageRegistry::catalogError() {
    // clang-format off
    static const bool built = [] { return !LanguageRegistryHelper::parsedCatalog().languages.isEmpty(); }();
    // clang-format on
    Q_UNUSED(built);
    return mutableCatalogError();
}

QMap<QString, HighlightRole> LanguageRegistryHelper::createSemanticRoles(const QJsonObject& catalog, Result<void>& outcome) {
    const QJsonObject declared = catalog.value(QStringLiteral("highlighting")).toObject().value(QStringLiteral("semantic")).toObject();
    QMap<QString, HighlightRole> values;

    for (auto entry = declared.constBegin(); entry != declared.constEnd(); ++entry) {
        const auto role = LanguageRegistryHelper::roleFromIdentifier(entry.value().toString());
        if (!role.has_value()) {
            reject(outcome, QStringLiteral("A catalog semantic token declares an unknown role"), entry.key());
            return {};
        }
        values.insert(entry.key(), *role);
    }

    if (values.isEmpty()) {
        reject(outcome, QStringLiteral("The catalog declares no semantic token"), {});
    }

    return values;
}

EditorLimits LanguageRegistryHelper::createLimits(const QJsonObject& catalog, Result<void>& outcome) {
    const QJsonObject declared = catalog.value(QStringLiteral("limits")).toObject();
    EditorLimits limits;
    bool valid = true;
    // clang-format off
    const auto read = [&declared, &valid](const QString& key, int minimum, int maximum) {
        const QJsonValue value = declared.value(key);

        if (!value.isDouble() || value.toInteger() < minimum || value.toInteger() > maximum) {
            valid = false;
            return 0LL;
        }

        return value.toInteger();
    };
    // clang-format on

    limits.maximumFileBytes = read(QStringLiteral("maximumFileBytes"), 1024, 64 * 1024 * 1024);
    limits.maximumHighlightedLineLength = static_cast<int>(read(QStringLiteral("maximumHighlightedLineLength"), 80, 100000));
    limits.maximumHighlightedMatchesPerLine = static_cast<int>(read(QStringLiteral("maximumHighlightedMatchesPerLine"), 32, 100000));
    limits.maximumSemanticTokenLines = static_cast<int>(read(QStringLiteral("maximumSemanticTokenLines"), 100, 1000000));
    limits.maximumSearchMatches = static_cast<int>(read(QStringLiteral("maximumSearchMatches"), 100, 1000000));
    limits.partialRepaintDivisor = static_cast<int>(read(QStringLiteral("partialRepaintDivisor"), 1, 100));
    limits.changeDebounceMs = static_cast<int>(read(QStringLiteral("changeDebounceMs"), 10, 5000));
    limits.analysisDebounceMs = static_cast<int>(read(QStringLiteral("analysisDebounceMs"), 10, 5000));
    limits.highlightDebounceMs = static_cast<int>(read(QStringLiteral("highlightDebounceMs"), 10, 5000));
    limits.externalChangeDebounceMs = static_cast<int>(read(QStringLiteral("externalChangeDebounceMs"), 10, 5000));
    limits.maximumRestarts = static_cast<int>(read(QStringLiteral("maximumRestarts"), 0, 100));
    limits.restartWindowMs = static_cast<int>(read(QStringLiteral("restartWindowMs"), 1000, 3600000));
    limits.initializeTimeoutMs = static_cast<int>(read(QStringLiteral("initializeTimeoutMs"), 1000, 600000));
    limits.maximumReferences = static_cast<int>(read(QStringLiteral("maximumReferences"), 10, 100000));
    limits.maximumWorkspaceFiles = static_cast<int>(read(QStringLiteral("maximumWorkspaceFiles"), 100, 1000000));
    limits.maximumProblems = static_cast<int>(read(QStringLiteral("maximumProblems"), 10, 100000));
    limits.maximumCompletions = static_cast<int>(read(QStringLiteral("maximumCompletions"), 10, 100000));
    limits.bottomPanelMinimumHeight = static_cast<int>(read(QStringLiteral("bottomPanelMinimumHeight"), 40, 2000));
    limits.bottomPanelInitialHeight = static_cast<int>(read(QStringLiteral("bottomPanelInitialHeight"), 40, 2000));

    // The analysis is asked for after the server already holds the change, because both waits start at the last keystroke.
    if (!valid || limits.bottomPanelInitialHeight < limits.bottomPanelMinimumHeight || limits.analysisDebounceMs <= limits.changeDebounceMs) {
        reject(outcome, QStringLiteral("The catalog limits are invalid"), {});
        return EditorLimits{};
    }

    return limits;
}

LanguageCatalog LanguageRegistry::parse(const QByteArray& text, Result<void>& outcome) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text, &error);

    if (error.error != QJsonParseError::NoError || !document.isObject() || !document.object().value(QStringLiteral("languages")).isArray() || !document.object().value(QStringLiteral("servers")).isArray()) {
        LanguageRegistryHelper::reject(outcome, QStringLiteral("The language catalog is not a catalog"), error.errorString());
        return {};
    }

    const QJsonObject root = document.object();
    LanguageCatalog catalog;
    catalog.languages = LanguageRegistryHelper::createLanguages(root, outcome);
    catalog.servers = LanguageRegistryHelper::createLanguageServers(root, catalog.languages, outcome);
    catalog.beforeKeywords = LanguageRegistryHelper::sharedPatterns(root, QStringLiteral("beforeKeywords"), outcome);
    catalog.afterKeywords = LanguageRegistryHelper::sharedPatterns(root, QStringLiteral("afterKeywords"), outcome);
    catalog.controlFlowKeywords = LanguageRegistryHelper::sharedKeywords(root, QStringLiteral("controlFlow"), outcome);
    catalog.primitiveTypeKeywords = LanguageRegistryHelper::sharedKeywords(root, QStringLiteral("primitiveTypes"), outcome);
    catalog.semanticRoles = LanguageRegistryHelper::createSemanticRoles(root, outcome);
    catalog.limits = LanguageRegistryHelper::createLimits(root, outcome);
    return catalog;
}

// The catalog is data, so a language, a server or a tunable is added by one entry in the file and never by interface code.
const LanguageCatalog& LanguageRegistryHelper::parsedCatalog() {
    // clang-format off
    static const LanguageCatalog value = [] {
        Result<void>& outcome = LanguageRegistry::mutableCatalogError();
        QFile file(QStringLiteral(":/workpane/code-editor/assets/languages.json"));

        if (!file.open(QIODevice::ReadOnly)) {
            LanguageRegistryHelper::reject(outcome, QStringLiteral("The language catalog is unavailable"), {});
            return LanguageCatalog{};
        }

        return LanguageRegistry::parse(file.readAll(), outcome);
    }();
    // clang-format on
    return value;
}

const QVector<HighlightPattern>& LanguageRegistry::patternsBeforeKeywords() {
    return LanguageRegistryHelper::parsedCatalog().beforeKeywords;
}

const QVector<HighlightPattern>& LanguageRegistry::patternsAfterKeywords() {
    return LanguageRegistryHelper::parsedCatalog().afterKeywords;
}

const QStringList& LanguageRegistry::controlFlowKeywords() {
    return LanguageRegistryHelper::parsedCatalog().controlFlowKeywords;
}

const QStringList& LanguageRegistry::primitiveTypeKeywords() {
    return LanguageRegistryHelper::parsedCatalog().primitiveTypeKeywords;
}

const QMap<QString, HighlightRole>& LanguageRegistry::semanticRoles() {
    return LanguageRegistryHelper::parsedCatalog().semanticRoles;
}

const EditorLimits& LanguageRegistry::limits() {
    return LanguageRegistryHelper::parsedCatalog().limits;
}

const LanguageDefinition* LanguageRegistry::languageForId(const QString& languageId) {
    for (const auto& language : languages()) {
        if (language.id == languageId) {
            return &language;
        }
    }

    return nullptr;
}

const QVector<LanguageDefinition>& LanguageRegistry::languages() {
    return LanguageRegistryHelper::parsedCatalog().languages;
}

const QVector<LanguageServerDefinition>& LanguageRegistry::languageServers() {
    return LanguageRegistryHelper::parsedCatalog().servers;
}

const LanguageDefinition& LanguageRegistry::languageForPath(const QString& path) {
    const QFileInfo information(path);

    for (const auto& language : languages()) {
        if (language.fileNames.contains(information.fileName(), Qt::CaseInsensitive) || language.extensions.contains(information.suffix(), Qt::CaseInsensitive)) {
            return language;
        }
    }

    static const LanguageDefinition fallback = LanguageRegistryHelper::plainText();
    return fallback;
}

// The protocol names C and C++ separately even though one server answers for both, so the identifier follows the file rather than the server.
QString LanguageRegistry::protocolLanguageId(const QString& path) {
    const LanguageDefinition& language = languageForPath(path);

    if (language.id == QStringLiteral("cpp") && QFileInfo(path).suffix() == QStringLiteral("c")) {
        return QStringLiteral("c");
    }

    return language.id;
}

std::optional<ResolvedLanguageServer> LanguageRegistry::resolveServer(const LanguageServerDefinition& definition) {
    for (const auto& candidate : definition.candidates) {
        const QString path = QStandardPaths::findExecutable(candidate.executableName);
        if (!path.isEmpty()) {
            return ResolvedLanguageServer{definition.languageId, path, candidate.arguments};
        }
    }

    return std::nullopt;
}

} // namespace workpane::plugins::codeeditor
