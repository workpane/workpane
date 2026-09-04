#pragma once

#include "AiTaskRepository.h"
#include "plugins/PluginInterface.h"
#include "ui/Components.h"
#include "ui/TabBar.h"

#include <QDialog>

class QLabel;
class QTableWidget;
class QPushButton;
class QTimer;
class QStackedWidget;

namespace workpane::plugins::ai {

class AiPlugin;
class AiConversationView;

class AiTaskInfoDialog final : public QDialog {
    Q_OBJECT

  public:
    AiTaskInfoDialog(AiPlugin& plugin, PluginHost& host, const AiTask& task, QWidget* parent);

    void showConversation();

  private:
    void updateRunState();
    void scheduleReload();
    [[nodiscard]] QString selectedExecutionId() const;
    void loadExecutions();
    void showExecutions(const Result<QVector<TaskExecution>>& loaded);
    void showLogs(const Result<QVector<ExecutionLogEntry>>& logs);
    void showPayload(const QString& title, const QString& payload);
    void showExecution(int row);
    [[nodiscard]] QString outputPlaceholder(const TaskExecution& execution) const;
    void showOutputPlaceholder(const QString& message);

    AiPlugin& m_plugin;
    PluginHost& m_host;
    AiTask m_task;
    ui::TabWidget* m_tabs{nullptr};
    QVector<TaskExecution> m_executions;
    QVector<ExecutionLogEntry> m_logEntries;
    AiConversationView* m_conversation{nullptr};
    ui::BusyIndicator* m_busy{nullptr};
    QLabel* m_phase{nullptr};
    QPushButton* m_stop{nullptr};
    QTimer* m_reload{nullptr};
    QLabel* m_status{nullptr};
    QTableWidget* m_executionGrid{nullptr};
    QTableWidget* m_logGrid{nullptr};
    QStackedWidget* m_outputPages{nullptr};
    ui::MarkdownView* m_content{nullptr};
    QLabel* m_outputEmpty{nullptr};
    quint64 m_logRevision{0};
};

} // namespace workpane::plugins::ai
