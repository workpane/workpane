#pragma once

#include "plugins/PluginManager.h"
#include "ui/SettingsView.h"

#include <QHash>
#include <QMainWindow>
#include <QTimer>

class QStackedWidget;
class QAction;

namespace workpane::app {
class ApplicationSettingsStore;
}

namespace workpane::ui {

class ModeBar;
class ToastOverlay;

class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    MainWindow(plugins::PluginManager& pluginManager, app::ApplicationSettingsStore& settings, QVector<CoreSettingsContribution> coreSettings = {}, QWidget* parent = nullptr);
    // The window opens showing that it is loading and only builds its interface once everything it presents is ready.
    [[nodiscard]] Result<void> buildInterface();
    [[nodiscard]] bool ready() const;
    void reloadTranslations();
    void reloadTheme();

  protected:
    void closeEvent(QCloseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private slots:
    void requestApplicationQuit();
    void selectMode(const QString& modeId);
    void showNotification(const QString& title, const QString& message, plugins::AlertSeverity severity);
    void persistWindowGeometry();

  private:
    void createActions();
    void showLoadingPage();
    [[nodiscard]] Result<void> createInterface(const QString& preferredModeId = {});
    void resetInterfacePointers();
    void scheduleWindowGeometrySave();

    plugins::PluginManager& m_pluginManager;
    app::ApplicationSettingsStore& m_settings;
    QVector<CoreSettingsContribution> m_coreSettings;
    QAction* m_quitAction{nullptr};
    ModeBar* m_modeBar{nullptr};
    QStackedWidget* m_contentStack{nullptr};
    ToastOverlay* m_toasts{nullptr};
    QHash<QString, QWidget*> m_views;
    QString m_currentModeId;
    QTimer m_windowGeometryTimer;
    bool m_ready{false};
};

} // namespace workpane::ui
