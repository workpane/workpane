#pragma once

#include "AiTaskRepository.h"
#include "plugins/PluginInterface.h"

#include <QDialog>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;

namespace workpane::plugins::ai {

class AiPlugin;

class AiAgentDialog final : public QDialog {
    Q_OBJECT

  public:
    AiAgentDialog(PluginHost& host, AiAgent agent, QStringList takenIdentifiers, const QVector<ModelConnection>& connections, QWidget* parent);

    [[nodiscard]] AiAgent agent() const;

  protected:
    void accept() override;

  private:
    void showTags();

    PluginHost& m_host;
    QStringList m_takenIdentifiers;
    QLineEdit* m_identifier{nullptr};
    QLineEdit* m_name{nullptr};
    QLineEdit* m_description{nullptr};
    QComboBox* m_connection{nullptr};
    QComboBox* m_template{nullptr};
    QSpinBox* m_maximumIterations{nullptr};
    QPlainTextEdit* m_systemPrompt{nullptr};
    QLabel* m_validation{nullptr};
};

class AiAgentSettingsView final : public QWidget {
    Q_OBJECT

  public:
    AiAgentSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent);

  private:
    void rebuild();
    void addAgent();
    void editAgent();
    void removeAgent();
    void persist(const QVector<AiAgent>& agents);
    [[nodiscard]] int selectedRow() const;

    AiPlugin& m_plugin;
    PluginHost& m_host;
    QVector<AiAgent> m_agents;
    QTableWidget* m_grid{nullptr};
    QLabel* m_empty{nullptr};
};

} // namespace workpane::plugins::ai
