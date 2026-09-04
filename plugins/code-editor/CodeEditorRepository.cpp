#include "CodeEditorRepository.h"

#include "CodeColorScheme.h"

#include "persistence/StoredValues.h"
#include "ui/Components.h"

#include <QDir>
#include <QSet>

#include <utility>

namespace workpane::plugins::codeeditor {

class CodeEditorRepositoryHelper final {
  public:
    static bool validWorkspace(const CodeWorkspaceState& workspace, int expectedPosition);
    static Result<void> validateState(const QVector<CodeWorkspaceState>& workspaces);
    static Result<QVector<CodeWorkspaceState>> invalidState(const QString& detail);
    static CodeEditorSettings settingsFromDocument(const QJsonObject& document);
    static QJsonObject settingsDocument(const CodeEditorSettings& settings);
};

bool CodeEditorRepositoryHelper::validWorkspace(const CodeWorkspaceState& workspace, int expectedPosition) {
    if (workspace.id.isEmpty() || !QDir::isAbsolutePath(workspace.rootPath) || workspace.position != expectedPosition || !workspace.createdAtUtc.isValid() || workspace.createdAtUtc.timeSpec() != Qt::UTC || !workspace.updatedAtUtc.isValid() || workspace.updatedAtUtc.timeSpec() != Qt::UTC || workspace.updatedAtUtc < workspace.createdAtUtc) {
        return false;
    }

    QSet<QString> paths;
    int activeDocuments = 0;

    for (int index = 0; index < workspace.documents.size(); ++index) {
        const auto& document = workspace.documents.at(index);
        if (!QDir::isAbsolutePath(document.path) || !document.path.startsWith(workspace.rootPath + QLatin1Char('/')) || document.position != index || document.cursorPosition < 0 || paths.contains(document.path)) {
            return false;
        }
        paths.insert(document.path);
        activeDocuments += document.active ? 1 : 0;
    }

    return activeDocuments <= 1 && (workspace.documents.isEmpty() || activeDocuments == 1);
}

Result<void> CodeEditorRepositoryHelper::validateState(const QVector<CodeWorkspaceState>& workspaces) {
    QSet<QString> ids;
    QSet<QString> roots;
    int activeWorkspaces = 0;

    for (int index = 0; index < workspaces.size(); ++index) {
        const auto& workspace = workspaces.at(index);
        if (!validWorkspace(workspace, index) || ids.contains(workspace.id) || roots.contains(workspace.rootPath)) {
            return Result<void>::failure({"code_editor_state_invalid", "The code editor state is invalid", workspace.rootPath});
        }
        ids.insert(workspace.id);
        roots.insert(workspace.rootPath);
        activeWorkspaces += workspace.active ? 1 : 0;
    }

    if (activeWorkspaces > 1 || (!workspaces.isEmpty() && activeWorkspaces != 1)) {
        return Result<void>::failure({"code_editor_state_invalid", "The code editor active workspace is invalid", {}});
    }

    return Result<void>::success();
}

Result<QVector<CodeWorkspaceState>> CodeEditorRepositoryHelper::invalidState(const QString& detail) {
    return Result<QVector<CodeWorkspaceState>>::failure({"code_editor_state_invalid", "The saved code editor state is invalid", detail});
}

CodeEditorRepository::CodeEditorRepository(PluginHost& host) : m_host(host) {}

Result<void> CodeEditorRepository::initialize() {
    return m_host.migrateDatabase({{1, {QStringLiteral("CREATE TABLE code_editor_workspaces(id TEXT PRIMARY KEY NOT NULL, root_path TEXT NOT NULL UNIQUE, position INTEGER NOT NULL UNIQUE CHECK(position >= 0), active INTEGER NOT NULL CHECK(active IN (0, 1)), created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT"), QStringLiteral("CREATE TABLE code_editor_documents(workspace_id TEXT NOT NULL REFERENCES code_editor_workspaces(id) ON DELETE CASCADE, path TEXT NOT NULL, position INTEGER NOT NULL CHECK(position >= 0), cursor_position INTEGER NOT NULL CHECK(cursor_position >= 0), active INTEGER NOT NULL CHECK(active IN (0, 1)), PRIMARY KEY(workspace_id, path), UNIQUE(workspace_id, position)) STRICT"), QStringLiteral("CREATE UNIQUE INDEX code_editor_active_workspace ON code_editor_workspaces(active) WHERE active = 1"), QStringLiteral("CREATE UNIQUE INDEX code_editor_active_document ON code_editor_documents(workspace_id, active) WHERE active = 1")}}});
}

Result<QVector<CodeWorkspaceState>> CodeEditorRepository::load() const {
    const auto workspaceRows = m_host.queryBootstrapDatabase(QStringLiteral("SELECT id, root_path, position, active, created_at_utc, updated_at_utc FROM code_editor_workspaces ORDER BY position"));

    if (!workspaceRows.hasValue()) {
        return Result<QVector<CodeWorkspaceState>>::failure(workspaceRows.error());
    }

    const auto documentRows = m_host.queryBootstrapDatabase(QStringLiteral("SELECT workspace_id, path, position, cursor_position, active FROM code_editor_documents ORDER BY workspace_id, position"));

    if (!documentRows.hasValue()) {
        return Result<QVector<CodeWorkspaceState>>::failure(documentRows.error());
    }

    QVector<CodeWorkspaceState> workspaces;
    QHash<QString, int> indexes;

    for (const auto& row : workspaceRows.value()) {
        CodeWorkspaceState workspace;
        workspace.id = row.value(QStringLiteral("id")).toString();
        workspace.rootPath = QDir::cleanPath(row.value(QStringLiteral("root_path")).toString());
        workspace.position = row.value(QStringLiteral("position")).toInt();
        workspace.active = row.value(QStringLiteral("active")).toInt() == 1;
        workspace.createdAtUtc = persistence::StoredValues::parseStoredTimestamp(row.value(QStringLiteral("created_at_utc")));
        workspace.updatedAtUtc = persistence::StoredValues::parseStoredTimestamp(row.value(QStringLiteral("updated_at_utc")));
        if (indexes.contains(workspace.id)) {
            return CodeEditorRepositoryHelper::invalidState(QStringLiteral("A workspace identifier is duplicated"));
        }
        if (!persistence::StoredValues::validStoredTimestamp(workspace.createdAtUtc) || !persistence::StoredValues::validStoredTimestamp(workspace.updatedAtUtc) || workspace.updatedAtUtc < workspace.createdAtUtc) {
            return CodeEditorRepositoryHelper::invalidState(QStringLiteral("A workspace timestamp is invalid"));
        }
        indexes.insert(workspace.id, static_cast<int>(workspaces.size()));
        workspaces.append(std::move(workspace));
    }

    for (const auto& row : documentRows.value()) {
        const QString workspaceId = row.value(QStringLiteral("workspace_id")).toString();
        if (!indexes.contains(workspaceId)) {
            return CodeEditorRepositoryHelper::invalidState(QStringLiteral("A document references an unknown workspace"));
        }
        workspaces[indexes.value(workspaceId)].documents.append({QDir::cleanPath(row.value(QStringLiteral("path")).toString()), row.value(QStringLiteral("position")).toInt(), row.value(QStringLiteral("cursor_position")).toInt(), row.value(QStringLiteral("active")).toInt() == 1});
    }

    const auto validation = CodeEditorRepositoryHelper::validateState(workspaces);
    return validation.hasValue() ? Result<QVector<CodeWorkspaceState>>::success(std::move(workspaces)) : CodeEditorRepositoryHelper::invalidState(validation.error().detail);
}

// Every value the settings document omits is the declared default, so a setting added later needs no schema and no stored row.
CodeEditorSettings CodeEditorRepositoryHelper::settingsFromDocument(const QJsonObject& document) {
    CodeEditorSettings settings;
    settings.colorSchemeId = CodeColorSchemeCatalog::defaultSchemeId();
    const CodeEditorSettings declared = settings;
    QString charsetName = EditorConfigs::textCharsetName(settings.defaultCharset);
    plugins::SettingsReader reader(document);
    reader.readBool(QStringLiteral("wordWrap"), settings.wordWrap);
    reader.readBool(QStringLiteral("languageServersEnabled"), settings.languageServersEnabled);
    reader.readText(QStringLiteral("fontFamily"), settings.fontFamily);
    reader.readInteger(QStringLiteral("fontSize"), settings.fontSize);
    reader.readText(QStringLiteral("defaultCharset"), charsetName);
    reader.readText(QStringLiteral("colorSchemeId"), settings.colorSchemeId);

    settings.defaultCharset = EditorConfigs::parseTextCharset(charsetName).value_or(declared.defaultCharset);

    if (!settings.fontFamily.isEmpty() && !ui::Components::monospacedFontFamilies().contains(settings.fontFamily)) {
        settings.fontFamily = declared.fontFamily;
    }

    if (!ui::ContentFontSizes::validContentFontSize(settings.fontSize)) {
        settings.fontSize = declared.fontSize;
    }

    if (!CodeColorSchemeCatalog::exists(settings.colorSchemeId)) {
        settings.colorSchemeId = declared.colorSchemeId;
    }

    return settings;
}

QJsonObject CodeEditorRepositoryHelper::settingsDocument(const CodeEditorSettings& settings) {
    return {{QStringLiteral("wordWrap"), settings.wordWrap}, {QStringLiteral("languageServersEnabled"), settings.languageServersEnabled}, {QStringLiteral("fontFamily"), settings.fontFamily}, {QStringLiteral("fontSize"), settings.fontSize}, {QStringLiteral("defaultCharset"), EditorConfigs::textCharsetName(settings.defaultCharset)}, {QStringLiteral("colorSchemeId"), settings.colorSchemeId}};
}

CodeEditorSettings CodeEditorRepository::loadSettings() const {
    return CodeEditorRepositoryHelper::settingsFromDocument(m_host.settings());
}

QFuture<Result<void>> CodeEditorRepository::saveSettings(const CodeEditorSettings& settings) {
    return m_host.saveSettings(CodeEditorRepositoryHelper::settingsDocument(settings));
}

QFuture<Result<void>> CodeEditorRepository::save(const QVector<CodeWorkspaceState>& workspaces) {
    const auto validation = CodeEditorRepositoryHelper::validateState(workspaces);

    if (!validation.hasValue()) {
        return QtFuture::makeReadyValueFuture(validation);
    }

    QVector<persistence::DatabaseStatement> statements = {{QStringLiteral("DELETE FROM code_editor_documents"), {}}, {QStringLiteral("DELETE FROM code_editor_workspaces"), {}}};

    for (const auto& workspace : workspaces) {
        statements.append({QStringLiteral("INSERT INTO code_editor_workspaces(id, root_path, position, active, created_at_utc, updated_at_utc) VALUES(?, ?, ?, ?, ?, ?)"), {workspace.id, workspace.rootPath, workspace.position, workspace.active ? 1 : 0, persistence::StoredValues::storedTimestamp(workspace.createdAtUtc), persistence::StoredValues::storedTimestamp(workspace.updatedAtUtc)}});
        for (const auto& document : workspace.documents) {
            statements.append({QStringLiteral("INSERT INTO code_editor_documents(workspace_id, path, position, cursor_position, active) VALUES(?, ?, ?, ?, ?)"), {workspace.id, document.path, document.position, document.cursorPosition, document.active ? 1 : 0}});
        }
    }

    return m_host.executeDatabaseTransaction(statements);
}

} // namespace workpane::plugins::codeeditor
