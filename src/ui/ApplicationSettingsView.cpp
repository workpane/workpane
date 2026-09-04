#include "ui/ApplicationSettingsView.h"

#include "app/ApplicationSettingsStore.h"
#include "app/ConfigurationManager.h"
#include "domain/ApplicationLanguage.h"
#include "persistence/CoreDatabaseSchema.h"
#include "ui/Components.h"
#include "ui/ConfirmationDialog.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace workpane::ui {

class ApplicationSettingsFactory final {
  public:
    ApplicationSettingsFactory(plugins::PluginManager& pluginManager, app::ApplicationSettingsStore& settings, app::ConfigurationManager& configurationManager) : m_pluginManager(pluginManager), m_settings(settings), m_configurationManager(configurationManager) {}

    QWidget* operator()(const QString& groupId, const QString& sectionId, QWidget* parent) const {
        if (groupId != QStringLiteral("application") || (sectionId != QStringLiteral("general") && sectionId != QStringLiteral("configuration"))) {
            return nullptr;
        }

        return new ApplicationSettingsView(m_pluginManager, m_settings, m_configurationManager, sectionId, parent);
    }

  private:
    plugins::PluginManager& m_pluginManager;
    app::ApplicationSettingsStore& m_settings;
    app::ConfigurationManager& m_configurationManager;
};

ApplicationSettingsView::ApplicationSettingsView(plugins::PluginManager& pluginManager, app::ApplicationSettingsStore& settings, app::ConfigurationManager& configurationManager, QString sectionId, QWidget* parent) : QWidget(parent), m_settings(settings), m_configurationManager(configurationManager), m_pluginManager(pluginManager) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    if (sectionId == QStringLiteral("general")) {
        auto* form = Components::settingsForm();
        m_language = new ui::ComboBox(pluginManager.theme(), this);
        m_language->setObjectName(QStringLiteral("applicationLanguage"));
        for (const auto& language : domain::ApplicationLanguages::applicationLanguageCatalog()) {
            m_language->addItem(pluginManager.translate(language.titleKey), language.code);
        }
        Components::sortComboBoxItems(m_language);
        m_language->setCurrentIndex(std::max(0, m_language->findData(m_settings.language())));
        Components::addSettingsRow(form, pluginManager.translate(QStringLiteral("workpane.application.language")), m_language);
        m_theme = new ui::ComboBox(pluginManager.theme(), this);
        m_theme->setObjectName(QStringLiteral("applicationTheme"));
        for (const auto& theme : ThemeManager::instance().catalog().themes()) {
            m_theme->addItem(pluginManager.translate(theme->titleKey()), theme->id());
        }
        Components::sortComboBoxItems(m_theme);
        m_theme->setCurrentIndex(std::max(0, m_theme->findData(m_settings.themeId())));
        Components::addSettingsRow(form, pluginManager.translate(QStringLiteral("workpane.application.theme")), m_theme);
        auto* version = new QLabel(QCoreApplication::applicationVersion(), this);
        version->setObjectName(QStringLiteral("applicationVersion"));
        Components::addSettingsRow(form, pluginManager.translate(QStringLiteral("workpane.application.version")), version);
        layout->addLayout(form);
        layout->addStretch(1);

        connect(m_language, &QComboBox::currentIndexChanged, this, &ApplicationSettingsView::selectLanguage);
        connect(m_theme, &QComboBox::currentIndexChanged, this, &ApplicationSettingsView::selectTheme);
        return;
    }

    const auto [actionRow, actions] = ui::Components::settingsActionRow(this);
    auto* importButton = new QPushButton(ui::IconCatalog::icon(ui::IconName::Import, pluginManager.theme()), pluginManager.translate(QStringLiteral("workpane.configuration.import")), actionRow);
    importButton->setObjectName(QStringLiteral("importConfiguration"));
    auto* exportButton = new QPushButton(ui::IconCatalog::icon(ui::IconName::Export, pluginManager.theme()), pluginManager.translate(QStringLiteral("workpane.configuration.export")), actionRow);
    exportButton->setObjectName(QStringLiteral("exportConfiguration"));
    actions->addWidget(importButton);
    actions->addWidget(exportButton);
    actions->addStretch(1);
    layout->addWidget(actionRow);
    layout->addStretch(1);

    connect(importButton, &QPushButton::clicked, this, &ApplicationSettingsView::importConfiguration);
    connect(exportButton, &QPushButton::clicked, this, &ApplicationSettingsView::exportConfiguration);
    // clang-format off
    connect(&m_configurationManager, &app::ConfigurationManager::transferStateChanged, this, [importButton, exportButton](bool active) {
        importButton->setEnabled(!active);
        exportButton->setEnabled(!active);
    });
    // clang-format on
}

void ApplicationSettingsView::selectLanguage(int index) {
    if (index < 0 || index >= m_language->count()) {
        return;
    }

    const auto result = m_settings.setLanguage(m_language->itemData(index).toString());

    if (!result.hasValue()) {
        m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("workpane.application.title")), m_pluginManager.translate(QStringLiteral("workpane.application.language-save-error")), plugins::AlertSeverity::Error);
    }
}

void ApplicationSettingsView::selectTheme(int index) {
    if (index < 0 || index >= m_theme->count()) {
        return;
    }

    const auto result = m_settings.setTheme(m_theme->itemData(index).toString());

    if (!result.hasValue()) {
        m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("workpane.application.title")), m_pluginManager.translate(QStringLiteral("workpane.application.theme-save-error")), plugins::AlertSeverity::Error);
    }
}

void ApplicationSettingsView::exportConfiguration() {
    const QString path = QFileDialog::getSaveFileName(this, m_pluginManager.translate(QStringLiteral("workpane.configuration.export-title")), QStringLiteral("workpane.sqlite3"), m_pluginManager.translate(QStringLiteral("workpane.configuration.file-filter")));

    if (path.isEmpty()) {
        return;
    }

    auto future = m_configurationManager.exportConfiguration(path);
    // clang-format off
    future.then(this, [this](Result<void> result) {
        m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("workpane.configuration.title")), result.hasValue() ? m_pluginManager.translate(QStringLiteral("workpane.configuration.export-success")) : result.error().message, result.hasValue() ? plugins::AlertSeverity::Success : plugins::AlertSeverity::Error);
    });
    // clang-format on
}

void ApplicationSettingsView::importConfiguration() {
    const QString path = QFileDialog::getOpenFileName(this, m_pluginManager.translate(QStringLiteral("workpane.configuration.import-title")), {}, m_pluginManager.translate(QStringLiteral("workpane.configuration.file-filter")));

    if (path.isEmpty()) {
        return;
    }

    const bool confirmed = ConfirmationDialog::confirm(this, m_pluginManager.translate(QStringLiteral("workpane.window.title")), m_pluginManager.translate(QStringLiteral("workpane.configuration.confirm-title")), m_pluginManager.translate(QStringLiteral("workpane.configuration.confirm-message")), m_pluginManager.translate(QStringLiteral("workpane.configuration.confirm-detail")), m_pluginManager.translate(QStringLiteral("workpane.configuration.import")), true);

    if (!confirmed) {
        return;
    }

    auto future = m_configurationManager.importConfiguration(path);
    // clang-format off
    future.then(this, [this](Result<void> result) {
        if (!result.hasValue()) {
            m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("workpane.configuration.title")), m_pluginManager.translate(QStringLiteral("workpane.configuration.import-error")), plugins::AlertSeverity::Error);
            return;
        }
        m_configurationManager.requestRestart();
    });
    // clang-format on
}

QVector<CoreSettingsContribution> ApplicationSettingsContributions::applicationSettingsContributions(plugins::PluginManager& pluginManager, app::ApplicationSettingsStore& settings, app::ConfigurationManager& configurationManager) {
    const plugins::SettingsSection general{QStringLiteral("general"), QStringLiteral("workpane.application.general"), {QStringLiteral("workpane.application.language"), QStringLiteral("workpane.application.theme"), QStringLiteral("workpane.application.version"), QStringLiteral("workpane.application.english"), QStringLiteral("workpane.application.portuguese"), QStringLiteral("workpane.application.theme-green"), QStringLiteral("workpane.application.theme-blue"), QStringLiteral("workpane.application.theme-red")}};
    const plugins::SettingsSection configuration{QStringLiteral("configuration"), QStringLiteral("workpane.configuration.title"), {QStringLiteral("workpane.configuration.import"), QStringLiteral("workpane.configuration.export")}};
    const plugins::SettingsGroup application{QStringLiteral("application"), QStringLiteral("workpane.application.title"), {general, configuration}};
    return {{application, ApplicationSettingsFactory(pluginManager, settings, configurationManager)}};
}

} // namespace workpane::ui
