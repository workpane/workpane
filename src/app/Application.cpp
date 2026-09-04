#include "app/Application.h"

#include "app/ConfigurationManager.h"
#include "persistence/ConfigurationTransfer.h"
#include "ui/ApplicationSettingsView.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <memory>
#include <utility>

namespace workpane::app {

Application::Application(QObject* parent) : Application(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation), parent) {}

Application::Application(QString dataPath, QObject* parent) : QObject(parent), m_dataPath(std::move(dataPath)) {}

Result<QString> Application::resolveDataPath(const QStringList& arguments) {
    QCommandLineParser parser;
    const QCommandLineOption dataDirectory(QStringLiteral("data-dir"), QStringLiteral("Keep the application data in this absolute directory."), QStringLiteral("path"));
    parser.addOption(dataDirectory);
    parser.addHelpOption();
    parser.addVersionOption();

    if (!parser.parse(arguments)) {
        return Result<QString>::failure({"application_arguments_invalid", "The application arguments are invalid", parser.errorText()});
    }
    if (!parser.isSet(dataDirectory)) {
        return Result<QString>::success(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    }

    const QString chosen = parser.value(dataDirectory);

    if (chosen.isEmpty() || !QDir::isAbsolutePath(chosen) || !QDir().mkpath(chosen)) {
        return Result<QString>::failure({"application_data_path_invalid", "The application data directory must be an absolute path that can be created", chosen});
    }

    return Result<QString>::success(QDir(chosen).absolutePath());
}

Application::~Application() {
    shutdown();
}

Result<void> Application::initialize() {
    if (m_shutdownComplete) {
        return Result<void>::failure({"application_shutdown_complete", "The application has already shut down", {}});
    }
    if (m_instanceLock != nullptr) {
        return Result<void>::failure({"application_already_initialized", "The application is already initialized", {}});
    }
    if (m_dataPath.isEmpty() || !QDir().mkpath(m_dataPath)) {
        return Result<void>::failure({"application_data_directory_failed", "The application data directory is unavailable", m_dataPath});
    }

    m_instanceLock = std::make_unique<QLockFile>(QDir(m_dataPath).filePath(QStringLiteral("instance.lock")));

    if (!m_instanceLock->tryLock() && (!m_instanceLock->removeStaleLockFile() || !m_instanceLock->tryLock())) {
        return Result<void>::failure({"application_already_running", "Another Workpane instance is already running", m_dataPath});
    }

    const auto pluginResult = m_pluginManager.loadPlugins();

    if (!pluginResult.hasValue()) {
        return pluginResult;
    }

    m_statePath = QDir(m_dataPath).filePath(QStringLiteral("workpane.sqlite3"));
    m_pendingImportPath = QDir(m_dataPath).filePath(QStringLiteral("workpane-import.sqlite3"));
    m_importBackupPath = QDir(m_dataPath).filePath(QStringLiteral("workpane-before-import.sqlite3"));
    const auto importResult = persistence::ConfigurationTransfer::beginPendingImport(m_statePath, m_pendingImportPath, m_importBackupPath, m_pluginManager.databaseSchemaVersions());

    if (!importResult.hasValue()) {
        return Result<void>::failure(importResult.error());
    }

    m_importInProgress = importResult.value();
    m_stateStore = std::make_unique<persistence::StateStore>(m_statePath);
    m_databaseExecutor = std::make_unique<persistence::DatabaseExecutor>(m_statePath);
    m_settings = std::make_unique<ApplicationSettingsStore>(*m_stateStore, *m_databaseExecutor);
    m_configurationManager = std::make_unique<ConfigurationManager>(*m_databaseExecutor, m_pendingImportPath, m_pluginManager.databaseSchemaVersions());
    const auto settingsResult = m_settings->initialize();

    if (!settingsResult.hasValue()) {
        return settingsResult;
    }

    const auto localeResult = m_pluginManager.setLocale(m_settings->language());

    if (!localeResult.hasValue()) {
        return localeResult;
    }

    ui::ThemeManager::instance().loadTheme(m_settings->themeId());
    m_pluginManager.setTheme(ui::ThemeManager::instance().theme());
    QApplication::setPalette(ui::ThemeManager::instance().theme().palette());
    QApplication::setFont(ui::ThemeManager::instance().theme().font(ui::ThemeFont::Interface));
    connect(m_settings.get(), &ApplicationSettingsStore::languageChanged, this, &Application::applyLanguage);
    connect(m_settings.get(), &ApplicationSettingsStore::themeChanged, this, &Application::applyTheme);
    connect(m_configurationManager.get(), &ConfigurationManager::restartRequested, this, &Application::restartAfterImport);

    const auto shutdownState = m_stateStore->wasCleanShutdown();

    if (!shutdownState.hasValue()) {
        return Result<void>::failure(shutdownState.error());
    }

    const bool recovered = !shutdownState.value();
    const auto markerResult = m_stateStore->markShutdown(false);

    if (!markerResult.hasValue()) {
        return markerResult;
    }

    m_recoveredFromUncleanShutdown = recovered;
    return Result<void>::success();
}

// Everything the reader waits for happens here, with the window already on screen saying that it is loading.
Result<void> Application::completeStartup() {
    // A window closed while it was loading leaves nothing to start, which is an ending rather than a failure.
    if (m_shutdownComplete) {
        return Result<void>::success();
    }
    if (m_settings == nullptr || m_mainWindow == nullptr) {
        return Result<void>::failure({"application_not_initialized", "The application is not initialized", {}});
    }

    const auto runtimeResult = initializePlugins();

    if (!runtimeResult.hasValue()) {
        return runtimeResult;
    }

    const auto built = m_mainWindow->buildInterface();

    if (!built.hasValue()) {
        return built;
    }

    // A database that had to be replaced is named to the reader once, because the file it was kept in is where its data still is.
    if (!m_stateStore->replacedDatabasePath().isEmpty()) {
        const QString backup = m_stateStore->replacedDatabasePath();
        // clang-format off
        const auto showReplacedAlert = [this, backup]() {
            m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("workpane.database.title")), m_pluginManager.translate(QStringLiteral("workpane.database.replaced")).arg(backup), plugins::AlertSeverity::Warning);
        };
        QMetaObject::invokeMethod(&m_pluginManager, showReplacedAlert, Qt::QueuedConnection);
        // clang-format on
    }

    if (!m_stateStore->rebuiltSchemas().isEmpty()) {
        const QString names = m_stateStore->rebuiltSchemas().join(QStringLiteral(", "));
        // clang-format off
        const auto showRebuiltAlert = [this, names]() {
            m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("workpane.database.title")), m_pluginManager.translate(QStringLiteral("workpane.database.rebuilt")).arg(names), plugins::AlertSeverity::Warning);
        };
        QMetaObject::invokeMethod(&m_pluginManager, showRebuiltAlert, Qt::QueuedConnection);
        // clang-format on
    }

    if (m_recoveredFromUncleanShutdown) {
        // clang-format off
        const auto showRecoveryAlert = [this]() {
            m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("workpane.recovery.title")), m_pluginManager.translate(QStringLiteral("workpane.recovery.message")), plugins::AlertSeverity::Warning);
        };
        QMetaObject::invokeMethod(&m_pluginManager, showRecoveryAlert, Qt::QueuedConnection);
        // clang-format on
    }

    if (m_importInProgress) {
        const auto finalizeImport = persistence::ConfigurationTransfer::finalizePendingImport(m_pendingImportPath, m_importBackupPath);
        if (!finalizeImport.hasValue()) {
            return finalizeImport;
        }
        m_importInProgress = false;
    }

    return Result<void>::success();
}

// The window opens saying that it is loading, so the reader sees the product immediately instead of waiting for everything it presents.
Result<void> Application::loadInterface() {
    if (m_settings == nullptr) {
        return Result<void>::failure({"application_not_initialized", "The application is not initialized", {}});
    }
    if (m_mainWindow != nullptr) {
        return Result<void>::failure({"application_interface_loaded", "The application interface is already loaded", {}});
    }

    m_mainWindow = std::make_unique<ui::MainWindow>(m_pluginManager, *m_settings, ui::ApplicationSettingsContributions::applicationSettingsContributions(m_pluginManager, *m_settings, *m_configurationManager));
    QCoreApplication::instance()->installEventFilter(this);
    m_quitEventFilterInstalled = true;
    m_mainWindow->show();
    return Result<void>::success();
}

void Application::applyLanguage(const QString& language) {
    const auto result = m_pluginManager.setLocale(language);

    if (!result.hasValue()) {
        qCritical().noquote() << result.error().message << result.error().detail;
        return;
    }

    if (m_mainWindow != nullptr) {
        m_mainWindow->reloadTranslations();
    }
}

void Application::applyTheme(const QString& themeId) {
    const auto result = ui::ThemeManager::instance().selectTheme(themeId);

    if (!result.hasValue()) {
        qCritical().noquote() << result.error().message << result.error().detail;
        return;
    }

    const auto& theme = ui::ThemeManager::instance().theme();
    m_pluginManager.setTheme(theme);
    QApplication::setPalette(theme.palette());
    QApplication::setFont(theme.font(ui::ThemeFont::Interface));

    if (m_mainWindow != nullptr) {
        m_mainWindow->reloadTheme();
    }
}

// The restart is asked for by the surface this tears down, so nothing is destroyed until that request has returned.
void Application::restartAfterImport() {
    // clang-format off
    QTimer::singleShot(0, this, [this]() { replaceProcess(); });
    // clang-format on
}

void Application::replaceProcess() {
    const QString executable = QCoreApplication::applicationFilePath();
    const QStringList arguments = QCoreApplication::arguments().mid(1);
    shutdown();

    if (!QProcess::startDetached(executable, arguments)) {
        qCritical().noquote() << "The application could not restart after importing configuration";
    }

    QCoreApplication::quit();
}

bool Application::eventFilter(QObject* watched, QEvent* event) {
    if (watched != QCoreApplication::instance() || event->type() != QEvent::Quit || m_mainWindow == nullptr || !m_mainWindow->isVisible()) {
        return QObject::eventFilter(watched, event);
    }

    return !m_mainWindow->close();
}

void Application::shutdown() {
    if (m_shutdownComplete) {
        return;
    }

    m_shutdownComplete = true;

    if (m_quitEventFilterInstalled && QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->removeEventFilter(this);
        m_quitEventFilterInstalled = false;
    }

    m_mainWindow.reset();
    m_pluginManager.shutdown();
    m_settings.reset();
    m_configurationManager.reset();
    m_databaseExecutor.reset();
    m_pluginManager.unloadPlugins();

    if (m_importInProgress) {
        m_stateStore.reset();
        const auto rollback = persistence::ConfigurationTransfer::rollbackPendingImport(m_statePath, m_pendingImportPath, m_importBackupPath);
        if (!rollback.hasValue()) {
            qCritical().noquote() << rollback.error().message << rollback.error().detail;
        }
        m_importInProgress = false;
    } else if (m_stateStore != nullptr) {
        const auto markerResult = m_stateStore->markShutdown(true);
        if (!markerResult.hasValue()) {
            qCritical().noquote() << markerResult.error().message << markerResult.error().detail;
        }
        m_stateStore.reset();
    }

    m_instanceLock.reset();
}

Result<void> Application::initializePlugins() {
    const auto pluginStateResult = m_pluginManager.initialize(m_dataPath, *m_stateStore, *m_databaseExecutor);

    if (!pluginStateResult.hasValue()) {
        return pluginStateResult;
    }

    return Result<void>::success();
}

} // namespace workpane::app
