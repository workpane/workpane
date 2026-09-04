#pragma once

#include "app/ApplicationSettingsStore.h"
#include "persistence/DatabaseExecutor.h"
#include "persistence/StateStore.h"
#include "plugins/PluginManager.h"

#include <QLockFile>
#include <QObject>

#include <memory>

namespace workpane::ui {
class MainWindow;
}

namespace workpane::app {

class ConfigurationManager;

class Application final : public QObject {
    Q_OBJECT

  public:
    explicit Application(QObject* parent = nullptr);
    Application(QString dataPath, QObject* parent);
    ~Application() override;

    // Where the application keeps its data, which is what the platform says unless the reader named a directory of their own.
    [[nodiscard]] static Result<QString> resolveDataPath(const QStringList& arguments);

    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] Result<void> loadInterface();
    [[nodiscard]] Result<void> completeStartup();
    void shutdown();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private slots:
    void restartAfterImport();

  private:
    void applyLanguage(const QString& language);
    void applyTheme(const QString& themeId);
    void replaceProcess();
    [[nodiscard]] Result<void> initializePlugins();

    plugins::PluginManager m_pluginManager;
    std::unique_ptr<QLockFile> m_instanceLock;
    std::unique_ptr<persistence::StateStore> m_stateStore;
    std::unique_ptr<persistence::DatabaseExecutor> m_databaseExecutor;
    std::unique_ptr<ApplicationSettingsStore> m_settings;
    std::unique_ptr<ConfigurationManager> m_configurationManager;
    std::unique_ptr<ui::MainWindow> m_mainWindow;
    QString m_dataPath;
    QString m_statePath;
    QString m_pendingImportPath;
    QString m_importBackupPath;
    bool m_quitEventFilterInstalled{false};
    bool m_shutdownComplete{false};
    bool m_importInProgress{false};
    bool m_recoveredFromUncleanShutdown{false};
};

} // namespace workpane::app
