#include "CodeEditorPlugin.h"

#include "CodeEditorTranslations.h"
#include "CodeEditorView.h"
#include "ui/ApplicationShortcuts.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <memory>
#include <utility>

namespace workpane::plugins::codeeditor {

QString CodeEditorPlugin::id() const {
    return QStringLiteral("code-editor");
}

QString CodeEditorPlugin::titleKey() const {
    return QStringLiteral("code-editor.plugin.title");
}

QStringList CodeEditorPlugin::dependencies() const {
    return {QStringLiteral("logs")};
}

int CodeEditorPlugin::databaseSchemaVersion() const {
    return 1;
}

TranslationCatalog CodeEditorPlugin::translations() const {
    return translations::CodeEditorCatalog::catalog();
}

QString CodeEditorPlugin::styleSheet(const ui::Theme&) const {
    return QStringLiteral("QSplitter#codeEditorSplitter::handle { background: @border; } QWidget#codeEditorSymbolPanel { background: @window; border-top: 1px solid @border; } QTreeView#codeEditorTree, QTreeWidget#codeEditorProblems, QTreeWidget#codeEditorReferences, QTreeWidget#codeEditorSymbols { background: @window; border: none; outline: none; alternate-background-color: @panel; } QTreeView#codeEditorTree::item, QTreeWidget#codeEditorProblems::item, QTreeWidget#codeEditorReferences::item, QTreeWidget#codeEditorSymbols::item { padding: 4px 6px; } QTreeView#codeEditorTree::item:hover, QTreeWidget#codeEditorProblems::item:hover, QTreeWidget#codeEditorReferences::item:hover, QTreeWidget#codeEditorSymbols::item:hover { background: @hover; } QListView#codeEditorCompletion { background: @panel; border: 1px solid @border; outline: none; } QListView#codeEditorCompletion::item { padding: 3px 8px; } QListView#codeEditorCompletion::item:selected { background: @accent; color: @onAccent; } QTreeView#codeEditorTree::item:selected, QTreeWidget#codeEditorProblems::item:selected, QTreeWidget#codeEditorReferences::item:selected, QTreeWidget#codeEditorSymbols::item:selected { background: @accent; color: @onAccent; }");
}

QVector<NavigationItem> CodeEditorPlugin::navigationItems(const ui::Theme& theme) const {
    return {{QStringLiteral("editor"), QStringLiteral("code-editor.navigation.editor"), ui::IconCatalog::icon(ui::IconName::Edit, theme), NavigationPlacement::Primary, NavigationOrder::Editor}};
}

QVector<SettingsGroup> CodeEditorPlugin::settingsGroups() const {
    const SettingsSection appearance{QStringLiteral("appearance"), QStringLiteral("code-editor.settings.appearance"), {QStringLiteral("code-editor.settings.font-family"), QStringLiteral("code-editor.settings.font-size"), QStringLiteral("code-editor.settings.word-wrap"), QStringLiteral("code-editor.settings.word-wrap-description")}};
    const SettingsSection languageServers{QStringLiteral("language-servers"), QStringLiteral("code-editor.settings.language-servers"), {QStringLiteral("code-editor.settings.language"), QStringLiteral("code-editor.settings.executable")}};
    const SettingsSection files{QStringLiteral("files"), QStringLiteral("code-editor.settings.files"), {QStringLiteral("code-editor.settings.default-charset")}};
    return {{QStringLiteral("editor"), QStringLiteral("code-editor.plugin.title"), {appearance, files, languageServers}}};
}

Result<void> CodeEditorPlugin::initialize(PluginHost& host) {
    if (m_host != nullptr) {
        return Result<void>::failure({"code_editor_already_initialized", "The Code Editor plugin is already initialized", {}});
    }
    if (const auto& catalog = LanguageRegistry::catalogError(); !catalog.hasValue()) {
        return catalog;
    }
    if (const auto& schemes = CodeColorSchemeCatalog::catalogError(); !schemes.hasValue()) {
        return schemes;
    }

    m_host = &host;
    m_asyncContext = std::make_unique<QObject>();
    m_repository = std::make_unique<CodeEditorRepository>(host);
    const auto migration = m_repository->initialize();

    if (!migration.hasValue()) {
        shutdown();
        return migration;
    }

    const auto state = m_repository->load();

    if (!state.hasValue()) {
        shutdown();
        return Result<void>::failure(state.error());
    }

    m_settings = m_repository->loadSettings();
    const CodeColorScheme* selected = CodeColorSchemeCatalog::scheme(m_settings.colorSchemeId);

    if (selected == nullptr) {
        shutdown();
        return Result<void>::failure({"code_editor_scheme_unavailable", "The selected code colour scheme is unavailable", m_settings.colorSchemeId});
    }

    const auto capability = host.provideCapability({QString::fromLatin1(openFolderCapability)});

    if (!capability.hasValue()) {
        shutdown();
        return capability;
    }

    m_colorScheme = *selected;
    m_workspaces = state.value();
    m_committedWorkspaces = m_workspaces;
    m_committedSettings = m_settings;
    m_persistenceTimer.setSingleShot(true);
    m_persistenceTimer.setInterval(250);
    connect(&m_persistenceTimer, &QTimer::timeout, this, &CodeEditorPlugin::persistState);
    refreshLanguageServers();
    return Result<void>::success();
}

QWidget* CodeEditorPlugin::createNavigationView(const QString& itemId, QWidget* parent) {
    if (itemId != QStringLiteral("editor") || m_host == nullptr) {
        return nullptr;
    }

    auto* view = new CodeEditorView(*this, parent);
    // clang-format off
    ui::ApplicationShortcuts::installContentFontShortcuts(view, [this](ui::ContentFontStep step) { stepEditorFontSize(step); });
    // clang-format on
    return view;
}

void CodeEditorPlugin::stepEditorFontSize(ui::ContentFontStep step) {
    if (m_repository == nullptr) {
        return;
    }
    if (step == ui::ContentFontStep::Reset) {
        setEditorFontSize(defaultEditorFontSize);
        return;
    }

    setEditorFontSize(ui::ContentFontSizes::steppedContentFontSize(m_settings.fontSize, step == ui::ContentFontStep::Increase ? 1 : -1));
}

QWidget* CodeEditorPlugin::createSettingsSection(const QString& groupId, const QString& sectionId, QWidget* parent) {
    if (groupId != QStringLiteral("editor") || m_host == nullptr) {
        return nullptr;
    }
    if (sectionId == QStringLiteral("appearance")) {
        return createAppearanceSection(parent);
    }
    if (sectionId == QStringLiteral("files")) {
        return createFilesSection(parent);
    }
    if (sectionId == QStringLiteral("language-servers")) {
        return createLanguageServersSection(parent);
    }

    return nullptr;
}

QWidget* CodeEditorPlugin::createFilesSection(QWidget* parent) {
    const auto [view, layout] = ui::Components::settingsSectionPage(parent);
    auto* form = ui::Components::settingsForm();
    auto* charset = new ui::ComboBox(m_host->theme(), view);
    charset->setObjectName(QStringLiteral("codeEditorDefaultCharset"));

    for (const auto declared : EditorConfigs::textCharsets()) {
        charset->addItem(EditorConfigs::textCharsetName(declared), EditorConfigs::textCharsetName(declared));
    }

    ui::Components::sortComboBoxItems(charset);
    charset->setCurrentIndex(std::max(0, charset->findData(EditorConfigs::textCharsetName(m_settings.defaultCharset))));
    charset->setToolTip(m_host->translate(QStringLiteral("code-editor.settings.default-charset-description")));
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("code-editor.settings.default-charset")), charset);
    layout->addLayout(form);
    // clang-format off
    connect(charset, &QComboBox::currentIndexChanged, this, [this, charset](int index) { setDefaultCharset(EditorConfigs::parseTextCharset(charset->itemData(index).toString()).value_or(TextCharset::Latin1)); });
    // clang-format on
    return view;
}

QWidget* CodeEditorPlugin::createAppearanceSection(QWidget* parent) {
    const auto [view, layout] = ui::Components::settingsSectionPage(parent);
    auto* form = ui::Components::settingsForm();
    auto* fontFamily = new ui::ComboBox(m_host->theme(), view);
    fontFamily->setObjectName(QStringLiteral("codeEditorFontFamily"));
    fontFamily->addItem(m_host->translate(QStringLiteral("code-editor.settings.font-family-system")), QString{});

    for (const auto& family : ui::Components::monospacedFontFamilies()) {
        fontFamily->addItem(family, family);
    }

    ui::Components::sortComboBoxItems(fontFamily);
    fontFamily->setCurrentIndex(std::max(0, fontFamily->findData(m_settings.fontFamily)));
    auto* fontSize = new QSpinBox(view);
    fontSize->setObjectName(QStringLiteral("codeEditorFontSize"));
    fontSize->setRange(ui::minimumContentFontSize, ui::maximumContentFontSize);
    fontSize->setValue(m_settings.fontSize);
    auto* colorScheme = new ui::ComboBox(m_host->theme(), view);
    colorScheme->setObjectName(QStringLiteral("codeEditorColorScheme"));

    for (const auto& scheme : CodeColorSchemeCatalog::schemes()) {
        colorScheme->addItem(scheme.name, scheme.id);
    }

    ui::Components::sortComboBoxItems(colorScheme);
    colorScheme->setCurrentIndex(std::max(0, colorScheme->findData(m_settings.colorSchemeId)));
    auto* wordWrap = new QCheckBox(view);
    wordWrap->setObjectName(QStringLiteral("codeEditorWordWrapSetting"));
    wordWrap->setChecked(m_settings.wordWrap);
    wordWrap->setToolTip(m_host->translate(QStringLiteral("code-editor.settings.word-wrap-description")));
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("code-editor.settings.font-family")), fontFamily);
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("code-editor.settings.font-size")), ui::Components::stepperRow(fontSize, m_host->theme(), view));
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("code-editor.settings.color-scheme")), colorScheme);
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("code-editor.settings.word-wrap")), wordWrap);
    layout->addLayout(form);
    // clang-format off
    connect(wordWrap, &QCheckBox::toggled, this, [this](bool enabled) { setWordWrap(enabled); });
    connect(fontFamily, &QComboBox::currentIndexChanged, this, [this, fontFamily](int index) { setEditorFontFamily(fontFamily->itemData(index).toString()); });
    connect(this, &CodeEditorPlugin::wordWrapChanged, wordWrap, [this, wordWrap]() { const QSignalBlocker blocker(wordWrap); wordWrap->setChecked(m_settings.wordWrap); });
    connect(fontSize, &QSpinBox::valueChanged, this, [this](int pointSize) { setEditorFontSize(pointSize); });
    connect(this, &CodeEditorPlugin::editorFontChanged, fontSize, [this, fontSize]() { const QSignalBlocker blocker(fontSize); fontSize->setValue(m_settings.fontSize); });
    connect(colorScheme, &QComboBox::currentIndexChanged, this, [this, colorScheme](int index) { setColorScheme(colorScheme->itemData(index).toString()); });
    connect(this, &CodeEditorPlugin::colorSchemeChanged, colorScheme, [this, colorScheme]() { const QSignalBlocker blocker(colorScheme); colorScheme->setCurrentIndex(std::max(0, colorScheme->findData(m_settings.colorSchemeId))); });
    // clang-format on
    return view;
}

QWidget* CodeEditorPlugin::createLanguageServersSection(QWidget* parent) {
    const auto [view, layout] = ui::Components::settingsSectionPage(parent);
    auto* form = ui::Components::settingsForm();
    auto* enabled = new QCheckBox(view);
    enabled->setObjectName(QStringLiteral("codeEditorLanguageServersSetting"));
    enabled->setChecked(m_settings.languageServersEnabled);
    enabled->setToolTip(m_host->translate(QStringLiteral("code-editor.settings.language-servers-enabled-description")));
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("code-editor.settings.language-servers-enabled")), enabled);
    auto* table = ui::Components::dataGrid({m_host->translate(QStringLiteral("code-editor.settings.language")), m_host->translate(QStringLiteral("code-editor.settings.executable"))}, view);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    ui::Components::stretchGridColumn(table, 1);
    const auto [actions, actionsLayout] = ui::Components::settingsActionRow(view);
    auto* refresh = new QPushButton(ui::IconCatalog::icon(ui::IconName::Refresh, m_host->theme()), m_host->translate(QStringLiteral("code-editor.actions.refresh")), actions);
    refresh->setEnabled(!m_languageServerDiscoveryInFlight);
    refresh->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    actionsLayout->addWidget(refresh);
    actionsLayout->addStretch(1);
    layout->addLayout(form);
    layout->addWidget(table, 1);
    layout->addWidget(actions);
    // clang-format off
    const auto populate = [this, table]() {
        table->setRowCount(static_cast<int>(LanguageRegistry::languageServers().size()));
        for (int row = 0; row < LanguageRegistry::languageServers().size(); ++row) {
            const auto& definition = LanguageRegistry::languageServers().at(row);
            QString path;
            for (const auto& server : m_languageServers) { if (server.languageId == definition.languageId) { path = server.executablePath; break; } }
            table->setItem(row, 0, new QTableWidgetItem(LanguageRegistry::languageForId(definition.languageId)->name));
            table->setItem(row, 1, new QTableWidgetItem(path.isEmpty() ? m_host->translate(QStringLiteral("code-editor.settings.not-found")) : path));
        }
    };
    connect(this, &CodeEditorPlugin::languageServersChanged, view, populate);
    connect(this, &CodeEditorPlugin::languageServerDiscoveryStateChanged, refresh, [refresh](bool running) { refresh->setEnabled(!running); });
    connect(refresh, &QPushButton::clicked, this, &CodeEditorPlugin::refreshLanguageServers);
    connect(enabled, &QCheckBox::toggled, this, [this](bool active) { setLanguageServersEnabled(active); });
    // clang-format on
    populate();
    return view;
}

void CodeEditorPlugin::handleRequest(const QString&, const QString& topic, const QJsonObject& payload, PluginReply reply) {
    if (topic == QString::fromLatin1(openFolderCapability) && SettingsReaders::hasExactKeys(payload, {QStringLiteral("path")}) && payload.value(QStringLiteral("path")).isString()) {
        const auto result = openWorkspace(payload.value(QStringLiteral("path")).toString());
        if (result.hasValue()) {
            m_host->showNavigation(QStringLiteral("editor"));
        }
        reply(result.hasValue() ? Result<QJsonObject>::success({{QStringLiteral("workspaceId"), result.value()}}) : Result<QJsonObject>::failure({result.error().code, m_host->translate(QStringLiteral("code-editor.error.workspace-open")), result.error().detail}));
        return;
    }

    reply(SettingsReaders::unhandledTopic(topic));
}

void CodeEditorPlugin::shutdown() {
    m_persistenceTimer.stop();

    if (m_repository != nullptr && m_stateRevision != m_persistedRevision) {
        [[maybe_unused]] auto pendingPersistence = m_repository->save(m_workspaces);
    }

    m_asyncContext.reset();
    m_languageServerDiscoveryFuture.waitForFinished();
    // A transport thread outlives the client that asked it to end, so this waits for the ones still finishing before the library they run is unloaded.
    LanguageServerClient::drainTransports();
    m_repository.reset();
    m_workspaces.clear();
    m_committedWorkspaces.clear();
    m_settings = {};
    m_committedSettings = {};
    m_languageServers.clear();
    m_stateRevision = 0;
    m_persistedRevision = 0;
    m_persistenceInFlight = false;
    m_languageServerDiscoveryInFlight = false;
    m_host = nullptr;
}

const QVector<CodeWorkspaceState>& CodeEditorPlugin::workspaces() const {
    return m_workspaces;
}

bool CodeEditorPlugin::wordWrap() const {
    return m_settings.wordWrap;
}

TextCharset CodeEditorPlugin::defaultCharset() const {
    return m_settings.defaultCharset;
}

// A scheme the catalog does not declare is refused rather than normalised into another one.
void CodeEditorPlugin::setColorScheme(const QString& schemeId) {
    const CodeColorScheme* selected = CodeColorSchemeCatalog::scheme(schemeId);

    if (m_settings.colorSchemeId == schemeId || m_repository == nullptr || selected == nullptr) {
        return;
    }

    m_settings.colorSchemeId = schemeId;
    m_colorScheme = *selected;
    emit colorSchemeChanged();
    persistSettings();
}

const CodeColorScheme& CodeEditorPlugin::colorScheme() const {
    return m_colorScheme;
}

void CodeEditorPlugin::setEditorFontFamily(const QString& family) {
    if (m_settings.fontFamily == family || m_repository == nullptr || (!family.isEmpty() && !ui::Components::monospacedFontFamilies().contains(family))) {
        return;
    }

    m_settings.fontFamily = family;
    emit editorFontChanged();
    persistSettings();
}

void CodeEditorPlugin::setEditorFontSize(int pointSize) {
    if (m_settings.fontSize == pointSize || m_repository == nullptr || !ui::ContentFontSizes::validContentFontSize(pointSize)) {
        return;
    }

    m_settings.fontSize = pointSize;
    emit editorFontChanged();
    persistSettings();
}

// A file already open keeps the encoding it was read in, so the declared one governs the next file that carries no mark.
void CodeEditorPlugin::setDefaultCharset(TextCharset charset) {
    if (m_settings.defaultCharset == charset || m_repository == nullptr) {
        return;
    }

    m_settings.defaultCharset = charset;
    persistSettings();
}

void CodeEditorPlugin::setWordWrap(bool enabled) {
    if (m_settings.wordWrap == enabled || m_repository == nullptr) {
        return;
    }

    m_settings.wordWrap = enabled;
    emit wordWrapChanged();
    persistSettings();
}

void CodeEditorPlugin::setLanguageServersEnabled(bool enabled) {
    if (m_settings.languageServersEnabled == enabled || m_repository == nullptr) {
        return;
    }

    m_settings.languageServersEnabled = enabled;
    emit languageServersChanged();
    persistSettings();
}

void CodeEditorPlugin::persistSettings() {
    const CodeEditorSettings next = m_settings;
    const quint64 revision = ++m_settingsRevision;
    auto future = m_repository->saveSettings(next);
    // clang-format off
    future.then(m_asyncContext.get(), [this, next, revision](Result<void> result) {
        if (result.hasValue()) {
            if (revision > m_committedSettingsRevision) {
                m_committedSettings = next;
                m_committedSettingsRevision = revision;
            }
            return;
        }
        if (revision != m_settingsRevision) {
            return;
        }
        m_host->log(LogLevel::Error, QStringLiteral("code-editor.persistence"), result.error().message, {{QStringLiteral("detail"), result.error().detail}});
        m_host->notify(m_host->translate(QStringLiteral("code-editor.error.title")), m_host->translate(QStringLiteral("code-editor.error.operation")), AlertSeverity::Error);
        m_settings = m_committedSettings;
        emit editorFontChanged();
        emit wordWrapChanged();
        emit languageServersChanged();
    });
    // clang-format on
}

CodeEditorFont CodeEditorPlugin::editorFont() const {
    return {m_settings.fontFamily, m_settings.fontSize};
}

const QVector<ResolvedLanguageServer>& CodeEditorPlugin::languageServers() const {
    return m_languageServers;
}

QVector<ResolvedLanguageServer> CodeEditorPlugin::activeLanguageServers() const {
    return m_settings.languageServersEnabled ? m_languageServers : QVector<ResolvedLanguageServer>{};
}

bool CodeEditorPlugin::languageServersEnabled() const {
    return m_settings.languageServersEnabled;
}

PluginHost& CodeEditorPlugin::host() const {
    return *m_host;
}

Result<QString> CodeEditorPlugin::openWorkspace(const QString& rootPath) {
    const QFileInfo directory(QDir::cleanPath(rootPath));

    if (!directory.isAbsolute() || !directory.isDir() || !directory.isReadable()) {
        return Result<QString>::failure({"code_editor_workspace_invalid", "The code editor workspace is unavailable", rootPath});
    }

    const QString canonicalPath = directory.canonicalFilePath();

    for (const auto& existing : m_workspaces) {
        if (existing.rootPath == canonicalPath) {
            if (const auto activation = activateWorkspace(existing.id); !activation.hasValue()) {
                return Result<QString>::failure(activation.error());
            }

            emit workspacesChanged();
            return Result<QString>::success(existing.id);
        }
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();

    for (auto& existing : m_workspaces) {
        existing.active = false;
    }

    const QString workspaceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_workspaces.append({workspaceId, canonicalPath, static_cast<int>(m_workspaces.size()), true, now, now, {}});
    schedulePersistence();
    emit workspacesChanged();
    return Result<QString>::success(workspaceId);
}

Result<void> CodeEditorPlugin::closeWorkspace(const QString& workspaceId) {
    for (int index = 0; index < m_workspaces.size(); ++index) {
        if (m_workspaces.at(index).id != workspaceId) {
            continue;
        }
        const bool wasActive = m_workspaces.at(index).active;
        m_workspaces.removeAt(index);
        for (int position = 0; position < m_workspaces.size(); ++position) {
            m_workspaces[position].position = position;
            m_workspaces[position].active = wasActive ? position == std::min(index, static_cast<int>(m_workspaces.size()) - 1) : m_workspaces[position].active;
        }
        schedulePersistence();
        emit workspacesChanged();
        return Result<void>::success();
    }

    return Result<void>::failure({"code_editor_workspace_unknown", "The code editor workspace is unknown", workspaceId});
}

Result<void> CodeEditorPlugin::activateWorkspace(const QString& workspaceId) {
    bool found = false;

    for (auto& current : m_workspaces) {
        current.active = current.id == workspaceId;
        found = found || current.active;
    }

    if (!found) {
        return Result<void>::failure({"code_editor_workspace_unknown", "The code editor workspace is unknown", workspaceId});
    }

    schedulePersistence();
    return Result<void>::success();
}

Result<void> CodeEditorPlugin::moveWorkspace(int from, int to) {
    if (from < 0 || from >= m_workspaces.size() || to < 0 || to >= m_workspaces.size()) {
        return Result<void>::failure({"code_editor_workspace_position_invalid", "The code editor workspace position is invalid", QStringLiteral("%1 -> %2").arg(from).arg(to)});
    }

    m_workspaces.move(from, to);

    for (int index = 0; index < m_workspaces.size(); ++index) {
        m_workspaces[index].position = index;
    }

    schedulePersistence();
    return Result<void>::success();
}

Result<void> CodeEditorPlugin::updateWorkspace(CodeWorkspaceState workspaceState) {
    auto* existing = workspace(workspaceState.id);

    if (existing == nullptr || workspaceState.rootPath != existing->rootPath || workspaceState.createdAtUtc != existing->createdAtUtc) {
        return Result<void>::failure({"code_editor_workspace_update_invalid", "The code editor workspace update is invalid", workspaceState.id});
    }

    workspaceState.position = existing->position;
    workspaceState.active = existing->active;
    *existing = std::move(workspaceState);
    schedulePersistence();
    return Result<void>::success();
}

void CodeEditorPlugin::refreshLanguageServers() {
    if (m_languageServerDiscoveryInFlight) {
        return;
    }

    m_languageServerDiscoveryInFlight = true;
    emit languageServerDiscoveryStateChanged(true);
    // clang-format off
    m_languageServerDiscoveryFuture = QtConcurrent::run([]() {
        QVector<ResolvedLanguageServer> servers;
        for (const auto& definition : LanguageRegistry::languageServers()) {
            const auto resolved = LanguageRegistry::resolveServer(definition);
            if (resolved.has_value()) {
                servers.append(*resolved);
            }
        }
        return servers;
    });
    m_languageServerDiscoveryFuture.then(m_asyncContext.get(), [this](QVector<ResolvedLanguageServer> servers) { m_languageServers = std::move(servers); m_languageServerDiscoveryInFlight = false; emit languageServersChanged(); emit languageServerDiscoveryStateChanged(false); });
    // clang-format on
}

void CodeEditorPlugin::schedulePersistence() {
    ++m_stateRevision;
    m_persistenceTimer.start();
}

void CodeEditorPlugin::persistState() {
    if (m_persistenceInFlight || m_repository == nullptr) {
        return;
    }

    m_persistenceInFlight = true;
    const quint64 revision = m_stateRevision;
    const QVector<CodeWorkspaceState> snapshot = m_workspaces;
    auto future = m_repository->save(snapshot);
    // clang-format off
    future.then(m_asyncContext.get(), [this, revision, snapshot](Result<void> result) {
        m_persistenceInFlight = false;
        const bool hasNewerChanges = revision != m_stateRevision;
        if (result.hasValue()) {
            m_committedWorkspaces = snapshot;
            m_persistedRevision = revision;
        } else {
            m_host->log(LogLevel::Error, QStringLiteral("code-editor.persistence"), result.error().message, {{QStringLiteral("detail"), result.error().detail}});
            m_host->notify(m_host->translate(QStringLiteral("code-editor.error.title")), m_host->translate(QStringLiteral("code-editor.error.operation")), AlertSeverity::Error);
            if (!hasNewerChanges) {
                m_workspaces = m_committedWorkspaces;
                m_stateRevision = m_persistedRevision;
                emit workspacesChanged();
            }
        }
        if (hasNewerChanges) {
            m_persistenceTimer.start();
        }
    });
    // clang-format on
}

CodeWorkspaceState* CodeEditorPlugin::workspace(const QString& workspaceId) {
    for (auto& current : m_workspaces) {
        if (current.id == workspaceId) {
            return &current;
        }
    }

    return nullptr;
}

} // namespace workpane::plugins::codeeditor
