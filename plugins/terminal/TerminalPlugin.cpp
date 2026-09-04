#include "TerminalPlugin.h"

#include "TerminalTranslations.h"
#include "TerminalView.h"
#include "terminal/TerminalThemeCatalog.h"
#include "ui/ApplicationShortcuts.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QJsonArray>
#include <QSpinBox>
#include <QVBoxLayout>

#include <memory>

namespace workpane::plugins::terminalplugin {

constexpr auto pluginIdentifier = "terminal";

TerminalPlugin::TerminalPlugin() = default;

TerminalPlugin::~TerminalPlugin() {
    shutdown();
}

QString TerminalPlugin::id() const {
    return QString::fromLatin1(pluginIdentifier);
}

QString TerminalPlugin::titleKey() const {
    return QStringLiteral("terminal.plugin.title");
}

QStringList TerminalPlugin::dependencies() const {
    return {QStringLiteral("logs")};
}

int TerminalPlugin::databaseSchemaVersion() const {
    return 1;
}

TranslationCatalog TerminalPlugin::translations() const {
    return translations::TerminalCatalog::catalog();
}

QString TerminalPlugin::styleSheet(const ui::Theme&) const {
    return QStringLiteral(R"(
        QWidget#workspaceBar { background: @panel; }
        QFrame#workspaceSlot { background: @window; border: none; }
        QFrame#slotDropIndicator { background: transparent; border: 2px solid @accent; border-radius: @controlRadiuspx; }
        QWidget#terminalGrid { background: @raised; }
        QWidget#emptySlot { background: @window; }
        QFrame#terminalPane { background: @terminal; border: none; }
        QWidget#terminalHeader { background: @panel; border: none; }
        QWidget#terminalHeader[active="true"] { background: @hover; }
        QWidget#terminalExitBar { background: @raised; border: none; }
        QFrame#sessionShelf { background: @panel; border-top: 1px solid @raised; }
        QFrame#sessionShelf[dropActive="true"] { background: @hover; border-top: 2px solid @accent; }
        QScrollArea#sessionShelfScroll, QWidget#sessionShelfContents { background: transparent; border: none; }
        QWidget#shelfChip { background: @raised; border: none; border-radius: @controlRadiuspx; }
        QWidget#shelfChip:hover { background: @hover; }
        QToolButton#layoutPresetButton { background: transparent; border: 1px solid transparent; border-radius: @controlRadiuspx; }
        QToolButton#layoutPresetButton:hover { background: @hover; }
        QToolButton#layoutPresetButton:checked { background: @hover; border-color: @accent; }
        QMenu#layoutMenu { background: @raised; border: 1px solid @border; padding: 5px; }
    )");
}

QVector<NavigationItem> TerminalPlugin::navigationItems(const ui::Theme& theme) const {
    return {{QStringLiteral("workspace"), QStringLiteral("terminal.navigation.workspace"), ui::IconCatalog::icon(ui::IconName::Terminal, theme), NavigationPlacement::Primary, NavigationOrder::Terminal}};
}

QVector<SettingsGroup> TerminalPlugin::settingsGroups() const {
    const SettingsSection general{QStringLiteral("general"), QStringLiteral("terminal.settings.general"), {QStringLiteral("terminal.settings.font-family"), QStringLiteral("terminal.settings.font-size"), QStringLiteral("terminal.settings.color-intensity"), QStringLiteral("terminal.settings.confirm-paste"), QStringLiteral("terminal.settings.allow-clipboard-write")}};
    return {{QStringLiteral("terminal"), QStringLiteral("terminal.plugin.title"), {general}}};
}

Result<void> TerminalPlugin::initialize(PluginHost& host) {
    if (m_host != nullptr) {
        return Result<void>::failure({"terminal_already_initialized", "The Terminal plugin is already initialized", {}});
    }

    m_host = &host;

    const auto migrationResult = host.migrateDatabase({{1, {QStringLiteral("CREATE TABLE terminal_state(scope_id TEXT PRIMARY KEY CHECK(scope_id IN ('preferences', 'workspace')), data_json TEXT NOT NULL) STRICT")}}});

    if (!migrationResult.hasValue()) {
        shutdown();
        return migrationResult;
    }

    const QString historyPath = QDir(host.applicationDataPath()).filePath(QStringLiteral("history"));

    if (!QDir().mkpath(historyPath)) {
        shutdown();
        return Result<void>::failure({"terminal_history_unavailable", "The terminal history directory is unavailable", historyPath});
    }

    m_settings = std::make_unique<TerminalSettingsStore>(host);
    const auto settingsResult = m_settings->initialize();

    if (!settingsResult.hasValue()) {
        shutdown();
        return settingsResult;
    }

    const auto* selectedTheme = terminalcore::TerminalThemes::terminalTheme(m_settings->themeId());

    if (selectedTheme == nullptr) {
        shutdown();
        return Result<void>::failure({"terminal_theme_unknown", "The stored terminal theme is unknown", m_settings->themeId()});
    }

    const auto capability = host.provideCapability({QString::fromLatin1(terminalSnapshotCapability)});

    if (!capability.hasValue()) {
        shutdown();
        return capability;
    }

    m_repository = std::make_unique<TerminalWorkspaceRepository>(host);
    m_manager = std::make_unique<plugins::terminalplugin::workspace::WorkspaceManager>(*m_repository, host, historyPath, *selectedTheme, terminalcore::PtyBackends::createSystemPtyBackend);
    const auto workspaceResult = m_manager->initialize();

    if (!workspaceResult.hasValue()) {
        shutdown();
        return workspaceResult;
    }

    connect(m_settings.get(), &TerminalSettingsStore::themeChanged, m_manager.get(), &plugins::terminalplugin::workspace::WorkspaceManager::setTerminalTheme);
    connect(m_settings.get(), &TerminalSettingsStore::allowClipboardWriteChanged, m_manager.get(), &plugins::terminalplugin::workspace::WorkspaceManager::setClipboardWriteAllowed);
    // clang-format off
    const auto forwardNotification = [this](const QString& title, const QString& message, bool error) {
        m_host->notify(title, message, error ? AlertSeverity::Error : AlertSeverity::Information);
    };
    const auto publishTerminalClosed = [this](const QString& terminalId) {
        m_host->publish(QStringLiteral("terminal.closed"), QJsonObject{{QStringLiteral("terminalId"), terminalId}});
    };
    const auto publishChange = [this]() {
        publishWorkspaceChanged();
    };
    const auto publishRenamedChange = [publishChange](const QString&) {
        publishChange();
    };
    // clang-format on
    connect(m_manager.get(), &plugins::terminalplugin::workspace::WorkspaceManager::notificationRequested, this, forwardNotification);
    connect(m_manager.get(), &plugins::terminalplugin::workspace::WorkspaceManager::terminalClosing, this, publishTerminalClosed);
    connect(m_manager.get(), &plugins::terminalplugin::workspace::WorkspaceManager::sessionsChanged, this, publishChange);
    connect(m_manager.get(), &plugins::terminalplugin::workspace::WorkspaceManager::currentTabChanged, this, publishChange);
    connect(m_manager.get(), &plugins::terminalplugin::workspace::WorkspaceManager::sessionNameChanged, this, publishRenamedChange);
    return Result<void>::success();
}

QWidget* TerminalPlugin::createNavigationView(const QString& itemId, QWidget* parent) {
    if (itemId != QStringLiteral("workspace") || m_manager == nullptr || m_settings == nullptr) {
        return nullptr;
    }

    auto* view = new TerminalView(*m_manager, *m_settings, *m_host, parent);
    // clang-format off
    ui::ApplicationShortcuts::installContentFontShortcuts(view, [this](ui::ContentFontStep step) { m_settings->stepFontSize(step); });
    // clang-format on
    return view;
}

QWidget* TerminalPlugin::createSettingsSection(const QString& groupId, const QString& sectionId, QWidget* parent) {
    if (groupId != QStringLiteral("terminal") || sectionId != QStringLiteral("general") || m_settings == nullptr) {
        return nullptr;
    }

    const auto [page, layout] = ui::Components::settingsSectionPage(parent);

    auto* form = ui::Components::settingsForm();
    auto* fontFamily = new ui::ComboBox(m_host->theme(), page);
    fontFamily->addItems(ui::Components::monospacedFontFamilies());
    ui::Components::sortComboBoxItems(fontFamily);
    fontFamily->setCurrentText(m_settings->fontFamily());
    auto* theme = new ui::ComboBox(m_host->theme(), page);

    for (const auto& item : terminalcore::TerminalThemes::terminalThemes()) {
        theme->addItem(item.name, item.id);
    }

    ui::Components::sortComboBoxItems(theme);
    theme->setCurrentIndex(theme->findData(m_settings->themeId()));
    auto* fontSize = new QSpinBox(page);
    fontSize->setObjectName(QStringLiteral("terminalFontSize"));
    fontSize->setRange(ui::minimumContentFontSize, ui::maximumContentFontSize);
    fontSize->setValue(m_settings->fontSize());
    auto* confirmPaste = new QCheckBox(page);
    confirmPaste->setChecked(m_settings->confirmMultilinePaste());
    auto* allowClipboardWrite = new QCheckBox(page);
    allowClipboardWrite->setObjectName(QStringLiteral("terminalAllowClipboardWrite"));
    allowClipboardWrite->setChecked(m_settings->allowClipboardWrite());
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("terminal.settings.font-family")), fontFamily);
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("terminal.settings.font-size")), ui::Components::stepperRow(fontSize, m_host->theme(), page));
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("terminal.settings.color-intensity")), theme);
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("terminal.settings.confirm-paste")), confirmPaste);
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("terminal.settings.allow-clipboard-write")), allowClipboardWrite);
    layout->addLayout(form);
    layout->addStretch(1);

    connect(fontFamily, &QComboBox::currentTextChanged, m_settings.get(), &TerminalSettingsStore::setFontFamily);
    connect(fontSize, &QSpinBox::valueChanged, m_settings.get(), &TerminalSettingsStore::setFontSize);
    // clang-format off
    connect(m_settings.get(), &TerminalSettingsStore::fontSizeChanged, fontSize, [fontSize](int pointSize) { const QSignalBlocker blocker(fontSize); fontSize->setValue(pointSize); });
    // clang-format on
    // clang-format off
    connect(theme, &QComboBox::currentIndexChanged, m_settings.get(), [this, theme](int index) { m_settings->setThemeId(theme->itemData(index).toString()); });
    // clang-format on
    connect(confirmPaste, &QCheckBox::toggled, m_settings.get(), &TerminalSettingsStore::setConfirmMultilinePaste);
    connect(allowClipboardWrite, &QCheckBox::toggled, m_settings.get(), &TerminalSettingsStore::setAllowClipboardWrite);
    return page;
}

void TerminalPlugin::handleRequest(const QString&, const QString& topic, const QJsonObject& payload, PluginReply reply) {
    if (topic == QString::fromLatin1(terminalSnapshotCapability) && payload.isEmpty() && m_manager != nullptr) {
        reply(Result<QJsonObject>::success(workspaceSnapshot()));
        return;
    }

    reply(SettingsReaders::unhandledTopic(topic));
}

void TerminalPlugin::shutdown() {
    if (m_manager != nullptr) {
        m_manager->shutdown();
    }

    m_manager.reset();
    m_repository.reset();
    m_settings.reset();
    m_host = nullptr;
}

QJsonObject TerminalPlugin::workspaceSnapshot() const {
    QJsonArray terminals;

    for (const auto& value : m_manager->allSessions()) {
        const QVariantMap session = value.toMap();
        terminals.append(QJsonObject{{QStringLiteral("id"), session.value(QStringLiteral("id")).toString()}, {QStringLiteral("name"), session.value(QStringLiteral("name")).toString()}, {QStringLiteral("cwd"), session.value(QStringLiteral("cwd")).toString()}});
    }

    return {{QStringLiteral("activeTerminalId"), m_manager->currentFocusedSessionId()}, {QStringLiteral("terminals"), terminals}};
}

void TerminalPlugin::publishWorkspaceChanged() {
    m_host->publish(QStringLiteral("terminal.workspace.changed"), {});
}

} // namespace workpane::plugins::terminalplugin
