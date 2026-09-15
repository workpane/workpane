#pragma once

#include "AiTaskRepository.h"
#include "ui/TabBar.h"

#include <QHash>
#include <QWidget>

#include <optional>

class QStackedWidget;
class QTabWidget;
class QToolButton;

namespace workpane::plugins {
class PluginHost;
}

namespace workpane::plugins::ai {

class AiPlugin;
class AiConversationView;
class TaskCard;
class KanbanColumn;

class AiTasksView final : public QWidget {
    Q_OBJECT

  public:
    AiTasksView(AiPlugin& plugin, PluginHost& host, QWidget* parent = nullptr);

  private:
    void synchronizeWorkspaces();
    void refreshKanban();
    void createWorkspace();
    void renameWorkspace();
    void removeWorkspace();
    void createTask();
    void editTask(std::optional<AiTask> task);
    void removeSchedule(const AiTask& task);
    void requestFolderDestination(const QString& capability, const QString& path);
    void openTaskSurface(const AiTask& task, bool onConversation);
    void showError(const Error& error, const QString& message);
    [[nodiscard]] QString moveFailureMessage(const QString& taskId, const Error& error) const;
    [[nodiscard]] QString activeWorkspaceId() const;
    [[nodiscard]] TaskCard* createCard(const AiTask& task);

    AiPlugin& m_plugin;
    PluginHost& m_host;
    QStackedWidget* m_stack{nullptr};
    ui::TabBar* m_workspaces{nullptr};
    QWidget* m_workspaceSeparator{nullptr};
    QWidget* m_empty{nullptr};
    QWidget* m_kanban{nullptr};
    QHash<TaskColumn, KanbanColumn*> m_columns;
    QHash<QString, TaskCard*> m_cards;
    bool m_synchronizing{false};
};

} // namespace workpane::plugins::ai
