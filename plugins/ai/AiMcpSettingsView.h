#pragma once

#include "agent/mcp/McpClient.h"
#include "plugins/PluginInterface.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QPlainTextEdit;

namespace workpane::ui {
class SecretField;
}

namespace workpane::plugins::ai {

class AiPlugin;

class AiMcpServerDialog final : public QDialog {
    Q_OBJECT

  public:
    AiMcpServerDialog(PluginHost& host, agent::mcp::McpServerDescriptor server, QStringList takenIdentifiers, QWidget* parent);

    [[nodiscard]] agent::mcp::McpServerDescriptor server() const;

  private:
    void applyTransport();
    void accept() override;

    PluginHost& m_host;
    QStringList m_takenIdentifiers;
    QLineEdit* m_identifier{nullptr};
    QComboBox* m_transport{nullptr};
    QLineEdit* m_command{nullptr};
    QLineEdit* m_arguments{nullptr};
    QLineEdit* m_workdir{nullptr};
    QLineEdit* m_address{nullptr};
    ui::SecretField* m_apiKey{nullptr};
    QPlainTextEdit* m_roots{nullptr};
    QCheckBox* m_sampling{nullptr};
    QSpinBox* m_samplingTokens{nullptr};
    QLabel* m_validation{nullptr};
};

class AiMcpSettingsView final : public QWidget {
    Q_OBJECT

  public:
    AiMcpSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent);

  private:
    void rebuild();
    void addServer();
    void editServer();
    void removeServer();
    void persist(const QVector<agent::mcp::McpServerDescriptor>& servers);
    [[nodiscard]] int selectedRow() const;

    AiPlugin& m_plugin;
    PluginHost& m_host;
    QVector<agent::mcp::McpServerDescriptor> m_servers;
    QTableWidget* m_grid{nullptr};
    QLabel* m_empty{nullptr};
};

} // namespace workpane::plugins::ai
