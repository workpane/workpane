#pragma once

#include "AiTaskRepository.h"
#include "plugins/PluginInterface.h"
#include "ui/Components.h"

#include <QDialog>

#include <optional>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QWidget;

namespace workpane::plugins::ai {

class AiTaskDialog final : public QDialog {
    Q_OBJECT

  public:
    AiTaskDialog(PluginHost& host, const QString& workspaceId, std::optional<AiTask> task, const ExecutionSettings& defaults, const QVector<AiAgent>& agents, QWidget* parent);

    [[nodiscard]] const AiTask& task() const;

  private:
    [[nodiscard]] QWidget* createGeneralPage();
    [[nodiscard]] QWidget* createPromptPage();
    void updateScheduleFields();
    void updateExecutionFields();
    void submit();
    void showValidation(const QString& messageKey);
    [[nodiscard]] std::optional<TaskSchedule> buildSchedule() const;

    PluginHost& m_host;
    QString m_workspaceId;
    std::optional<AiTask> m_original;
    ExecutionSettings m_defaults;
    QVector<AiAgent> m_agents;
    AiTask m_accepted;

    QLineEdit* m_title{nullptr};
    QLineEdit* m_description{nullptr};
    QLineEdit* m_issueUrl{nullptr};
    QComboBox* m_executionKind{nullptr};
    QComboBox* m_agent{nullptr};
    QLineEdit* m_workdir{nullptr};
    QLineEdit* m_command{nullptr};
    QSpinBox* m_commandTimeout{nullptr};
    QFormLayout* m_executionForm{nullptr};
    QPlainTextEdit* m_prompt{nullptr};
    QComboBox* m_scheduleKind{nullptr};
    ui::DateTimeField* m_scheduleAt{nullptr};
    QSpinBox* m_scheduleInterval{nullptr};
    QLineEdit* m_scheduleCron{nullptr};
    QFormLayout* m_scheduleForm{nullptr};
    QLabel* m_validation{nullptr};
};

} // namespace workpane::plugins::ai
