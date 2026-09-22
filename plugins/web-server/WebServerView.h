#pragma once

#include "WebServerPlugin.h"
#include "ui/Components.h"

#include <QDialog>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTableView;
class QTableWidget;
class QTimer;
class QToolButton;

namespace workpane::plugins::webserver {

class RequestTableModel;

class WebServerDialog final : public QDialog {
    Q_OBJECT

  public:
    WebServerDialog(QString serverId, QString terminalId, QString initialRoot, plugins::webserver::WebServerPlugin& plugin, QWidget* parent = nullptr);

  private slots:
    void chooseRoot();
    void startServer();
    void stopServer();
    void editServer();
    void openServer();
    void refreshState(const QString& changedServerId = {});
    void updateAddressPreview();

  private:
    void hideError();
    void loadConfiguration();
    void showError(const Error& error);

    QString m_serverId;
    QString m_terminalId;
    QString m_initialRoot;
    plugins::webserver::WebServerPlugin& m_plugin;
    ui::StatusIndicator* m_statusIndicator{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_addressLabel{nullptr};
    QLabel* m_errorLabel{nullptr};
    QLineEdit* m_nameEdit{nullptr};
    QLineEdit* m_rootEdit{nullptr};
    QLineEdit* m_hostEdit{nullptr};
    QSpinBox* m_portSpin{nullptr};
    QToolButton* m_rootButton{nullptr};
    QPushButton* m_startButton{nullptr};
    QPushButton* m_openButton{nullptr};
    QPushButton* m_editButton{nullptr};
    QPushButton* m_stopButton{nullptr};
};

class WebServerView final : public QWidget {
    Q_OBJECT

  public:
    explicit WebServerView(plugins::webserver::WebServerPlugin& plugin, QWidget* parent = nullptr);
    ~WebServerView() override;

  private slots:
    void refreshInstances(const QString& changedServerId = {});
    void refreshRequests();
    void selectServer();
    void createServer();
    void createServerFromTerminal();
    void editServerConfiguration();
    void openServerEditor(const QString& serverId);
    void openServerForFolder(const QString& serverId, const QString& root);
    void openDialog(WebServerDialog* dialog);
    void removeServerConfiguration();
    void startServer();
    void stopServer();
    void openServer();
    void openServerInBrowser();
    void clearRequests();
    void scheduleSplitterSave(int position, int index);
    void persistSplitterRatio();

  private:
    [[nodiscard]] QString selectedServerId() const;
    void resetRequestLog(const QString& serverId);

    plugins::webserver::WebServerPlugin& m_plugin;
    QLabel* m_summaryLabel{nullptr};
    QLabel* m_emptyLabel{nullptr};
    QLabel* m_requestTitle{nullptr};
    QWidget* m_requestPane{nullptr};
    QTableWidget* m_serverTable{nullptr};
    QTableView* m_requestTable{nullptr};
    RequestTableModel* m_requestModel{nullptr};
    QPushButton* m_clearRequestsButton{nullptr};
    QSplitter* m_splitter{nullptr};
    QTimer* m_refreshTimer{nullptr};
    QTimer* m_splitterSaveTimer{nullptr};
    QString m_requestServerId;
    quint64 m_requestCursor{};
    int m_pendingSplitterRatio{};
};

} // namespace workpane::plugins::webserver
