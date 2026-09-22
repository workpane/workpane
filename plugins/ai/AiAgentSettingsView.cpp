#include "AiAgentSettingsView.h"

#include "AiAgentPrompt.h"
#include "AiPlugin.h"
#include "AiProviderCatalog.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace workpane::plugins::ai {

constexpr int agentDialogMinimumWidth = 720;
constexpr int agentPromptMinimumHeight = 260;

class AiAgentSettingsViewHelper final {
  public:
    static const QRegularExpression& identifierPattern();
    static QString identifierFromName(const QString& name);
};

const QRegularExpression& AiAgentSettingsViewHelper::identifierPattern() {
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9-]{0,47}$"));
    return pattern;
}

// The identifier is spelled from the name, so the writer names the agent and receives an identifier that already obeys the rule.
QString AiAgentSettingsViewHelper::identifierFromName(const QString& name) {
    QString spelled;

    for (const QChar character : name.toLower()) {
        const bool accepted = (character >= QLatin1Char('a') && character <= QLatin1Char('z')) || (character >= QLatin1Char('0') && character <= QLatin1Char('9'));
        if (accepted) {
            spelled.append(character);
            continue;
        }
        if (!spelled.isEmpty() && !spelled.endsWith(QLatin1Char('-'))) {
            spelled.append(QLatin1Char('-'));
        }
    }

    while (!spelled.isEmpty() && !(spelled.front() >= QLatin1Char('a') && spelled.front() <= QLatin1Char('z'))) {
        spelled.remove(0, 1);
    }

    while (spelled.endsWith(QLatin1Char('-'))) {
        spelled.chop(1);
    }

    return spelled.left(48);
}

AiAgentDialog::AiAgentDialog(PluginHost& host, AiAgent agent, QStringList takenIdentifiers, const QVector<ModelConnection>& connections, QWidget* parent) : QDialog(parent), m_host(host), m_takenIdentifiers(std::move(takenIdentifiers)) {
    setObjectName(QStringLiteral("aiAgentDialog"));
    setWindowTitle(m_host.translate(agent.id.isEmpty() ? QStringLiteral("ai.agent.add") : QStringLiteral("ai.agent.edit")));
    setMinimumWidth(agentDialogMinimumWidth);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(12);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_name = new QLineEdit(agent.name, this);
    m_name->setObjectName(QStringLiteral("aiAgentName"));
    m_identifier = new QLineEdit(agent.id, this);
    m_identifier->setObjectName(QStringLiteral("aiAgentIdentifier"));
    m_identifier->setPlaceholderText(m_host.translate(QStringLiteral("ai.agent.identifier-placeholder")));
    m_description = new QLineEdit(agent.description, this);
    m_description->setObjectName(QStringLiteral("aiAgentDescription"));
    m_description->setPlaceholderText(m_host.translate(QStringLiteral("ai.agent.description-placeholder")));

    m_connection = new ui::ComboBox(m_host.theme(), this);
    m_connection->setObjectName(QStringLiteral("aiAgentConnection"));

    for (const auto& connection : connections) {
        m_connection->addItem(ModelConnections::connectionLabel(connection), ModelConnections::connectionKey(connection));
    }

    ui::Components::sortComboBoxItems(m_connection);
    m_connection->setCurrentIndex(std::max(0, m_connection->findData(agent.connectionKey)));

    m_maximumIterations = new QSpinBox(this);
    m_maximumIterations->setObjectName(QStringLiteral("aiAgentMaximumIterations"));
    m_maximumIterations->setRange(0, ProviderCatalog::aiLimits().maximumAgentIterations);
    m_maximumIterations->setValue(agent.maximumIterations);
    m_maximumIterations->setToolTip(m_host.translate(QStringLiteral("ai.task.unlimited-hint")));

    // The identifier follows the name until the writer types one of their own, and follows it again once they clear it.
    // clang-format on

    form->addRow(m_host.translate(QStringLiteral("ai.agent.name")), m_name);
    form->addRow(m_host.translate(QStringLiteral("ai.agent.identifier")), ui::Components::fieldWithHint(m_identifier, m_host.translate(QStringLiteral("ai.agent.identifier-hint")), this));
    form->addRow(m_host.translate(QStringLiteral("ai.agent.description")), m_description);
    form->addRow(m_host.translate(QStringLiteral("ai.agent.connection")), m_connection);
    form->addRow(m_host.translate(QStringLiteral("ai.agent.maximum-iterations")), ui::Components::stepperRow(m_maximumIterations, m_host.theme(), this));
    layout->addLayout(form);

    auto* promptActions = new QWidget(this);
    auto* promptActionsLayout = new QHBoxLayout(promptActions);
    promptActionsLayout->setContentsMargins(0, 0, 0, 0);
    promptActionsLayout->setSpacing(6);
    promptActionsLayout->addWidget(new QLabel(m_host.translate(QStringLiteral("ai.agent.system-prompt")), promptActions), 1);
    m_template = new ui::ComboBox(m_host.theme(), promptActions);
    m_template->setObjectName(QStringLiteral("aiAgentTemplate"));

    for (const auto& identifier : ProviderCatalog::promptTemplates()) {
        m_template->addItem(m_host.translate(QStringLiteral("ai.template.%1-name").arg(identifier)), identifier);
    }

    ui::Components::sortComboBoxItems(m_template);

    // What each template is for is said after the sort, because reordering the list keeps the text and the value alone.
    for (int index = 0; index < m_template->count(); ++index) {
        m_template->setItemData(index, m_host.translate(QStringLiteral("ai.template.%1-description").arg(m_template->itemData(index).toString())), Qt::ToolTipRole);
    }
    promptActionsLayout->addWidget(m_template);

    auto* insertTemplate = new QPushButton(m_host.translate(QStringLiteral("ai.agent.insert-template")), promptActions);
    insertTemplate->setObjectName(QStringLiteral("aiAgentInsertTemplate"));
    auto* showTagList = new QPushButton(m_host.translate(QStringLiteral("ai.agent.show-tags")), promptActions);
    showTagList->setObjectName(QStringLiteral("aiAgentShowTags"));
    promptActionsLayout->addWidget(insertTemplate);
    promptActionsLayout->addWidget(showTagList);
    layout->addWidget(promptActions);

    m_systemPrompt = new ui::TextField(m_host.translate(QStringLiteral("ai.agent.system-prompt-placeholder")), this);
    m_systemPrompt->setObjectName(QStringLiteral("aiAgentSystemPrompt"));
    m_systemPrompt->setPlainText(agent.systemPrompt);
    m_systemPrompt->setMinimumHeight(agentPromptMinimumHeight);
    layout->addWidget(m_systemPrompt, 1);

    m_validation = new QLabel(this);
    m_validation->setObjectName(QStringLiteral("aiTaskValidation"));
    m_validation->setWordWrap(true);
    m_validation->hide();
    layout->addWidget(m_validation);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primaryButton"));
    layout->addWidget(buttons);

    // clang-format off
    connect(insertTemplate, &QPushButton::clicked, this, [this]() { m_systemPrompt->setPlainText(m_host.translate(QStringLiteral("ai.template.%1-body").arg(m_template->currentData().toString()))); });
    connect(showTagList, &QPushButton::clicked, this, [this]() { showTags(); });
    // clang-format on
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

AiAgent AiAgentDialog::agent() const {
    AiAgent built;
    // An identifier nobody typed is spelled from the name when it is saved, rather than written into the field while the reader is still typing it.
    const QString typed = m_identifier->text().trimmed();
    built.id = typed.isEmpty() ? AiAgentSettingsViewHelper::identifierFromName(m_name->text()) : typed;
    built.name = m_name->text().trimmed();
    built.description = m_description->text().trimmed();
    built.systemPrompt = m_systemPrompt->toPlainText().trimmed();
    built.connectionKey = m_connection->currentData().toString();
    built.maximumIterations = m_maximumIterations->value();
    return built;
}

// The tags are shown where the prompt is written, because a writer who cannot see them writes one that does not exist.
void AiAgentDialog::showTags() {
    auto* dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("aiAgentTagsDialog"));
    dialog->setWindowTitle(m_host.translate(QStringLiteral("ai.agent.show-tags")));
    dialog->setMinimumWidth(agentDialogMinimumWidth);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(12);

    QStringList rows;

    for (const auto& tag : AgentPrompts::promptTags()) {
        rows.append(QStringLiteral("`{{%1}}` — %2").arg(tag.name, m_host.translate(tag.descriptionKey)));
    }

    auto* content = new ui::MarkdownView(m_host.theme(), dialog);
    content->setObjectName(QStringLiteral("aiAgentTagsContent"));
    content->setDocumentMarkdown(rows.join(QStringLiteral("\n\n")));
    layout->addWidget(content, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    // The answer is never read, so the loop that would keep running while this surface can be destroyed is not opened.
    ui::Components::showDialogWindow(dialog, m_host.translate(QStringLiteral("ai.agent.show-tags")));
}

void AiAgentDialog::accept() {
    const AiAgent candidate = agent();
    QString message;

    if (!AiAgentSettingsViewHelper::identifierPattern().match(candidate.id).hasMatch()) {
        message = m_host.translate(QStringLiteral("ai.validation.agent-identifier"));
    } else if (m_takenIdentifiers.contains(candidate.id)) {
        message = m_host.translate(QStringLiteral("ai.validation.agent-duplicate"));
    } else if (candidate.name.isEmpty()) {
        message = m_host.translate(QStringLiteral("ai.validation.agent-name"));
    } else if (candidate.connectionKey.isEmpty()) {
        message = m_host.translate(QStringLiteral("ai.validation.connection-missing"));
    } else if (candidate.systemPrompt.isEmpty()) {
        message = m_host.translate(QStringLiteral("ai.validation.agent-prompt"));
    } else if (const QStringList unknown = AgentPrompts::unknownPromptTags(candidate.systemPrompt); !unknown.isEmpty()) {
        message = m_host.translate(QStringLiteral("ai.validation.agent-tag")).arg(unknown.join(QStringLiteral(", ")));
    }

    if (!message.isEmpty()) {
        m_validation->setText(message);
        m_validation->show();
        ui::Components::growDialogToContents(this);
        return;
    }

    QDialog::accept();
}

AiAgentSettingsView::AiAgentSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_host(host), m_agents(plugin.agents()) {
    setObjectName(QStringLiteral("aiAgentSettings"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const auto [page, layout] = ui::Components::settingsSectionPage(this);

    const auto [actions, actionsLayout] = ui::Components::settingsActionRow(page);
    auto* add = ui::Components::toolButton(ui::IconName::Add, m_host.theme(), m_host.translate(QStringLiteral("ai.agent.add")), actions);
    add->setObjectName(QStringLiteral("aiAgentAdd"));
    auto* edit = ui::Components::toolButton(ui::IconName::Edit, m_host.theme(), m_host.translate(QStringLiteral("ai.agent.edit")), actions);
    edit->setObjectName(QStringLiteral("aiAgentEdit"));
    auto* remove = ui::Components::toolButton(ui::IconName::Close, m_host.theme(), m_host.translate(QStringLiteral("ai.agent.remove")), actions);
    remove->setObjectName(QStringLiteral("aiAgentRemove"));
    actionsLayout->addWidget(add);
    actionsLayout->addWidget(edit);
    actionsLayout->addWidget(remove);
    actionsLayout->addStretch(1);
    layout->addWidget(actions);

    const QStringList headers{m_host.translate(QStringLiteral("ai.agent.identifier")), m_host.translate(QStringLiteral("ai.agent.name")), m_host.translate(QStringLiteral("ai.agent.connection")), m_host.translate(QStringLiteral("ai.agent.description"))};
    m_grid = ui::Components::dataGrid(headers, page);
    m_grid->setObjectName(QStringLiteral("aiAgentGrid"));
    layout->addWidget(m_grid, 1);

    m_empty = ui::Components::emptyStateLabel(m_host.translate(QStringLiteral("ai.agent.empty")), page);
    m_empty->setObjectName(QStringLiteral("aiAgentEmpty"));
    layout->addWidget(m_empty, 1);

    connect(add, &QToolButton::clicked, this, &AiAgentSettingsView::addAgent);
    connect(edit, &QToolButton::clicked, this, &AiAgentSettingsView::editAgent);
    connect(remove, &QToolButton::clicked, this, &AiAgentSettingsView::removeAgent);
    // clang-format off
    connect(m_grid, &QTableWidget::doubleClicked, this, [this]() { editAgent(); });
    // clang-format on

    root->addWidget(page, 1);
    rebuild();
}

int AiAgentSettingsView::selectedRow() const {
    const auto rows = m_grid->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void AiAgentSettingsView::rebuild() {
    m_grid->setRowCount(static_cast<int>(m_agents.size()));

    for (int row = 0; row < static_cast<int>(m_agents.size()); ++row) {
        const AiAgent& agent = m_agents.at(row);
        m_grid->setItem(row, 0, new QTableWidgetItem(agent.id));
        m_grid->setItem(row, 1, new QTableWidgetItem(agent.name));
        m_grid->setItem(row, 2, new QTableWidgetItem(agent.connectionKey));
        m_grid->setItem(row, 3, new QTableWidgetItem(agent.description));
    }

    m_grid->setVisible(!m_agents.isEmpty());
    m_empty->setVisible(m_agents.isEmpty());
}

void AiAgentSettingsView::addAgent() {
    QStringList taken;

    for (const auto& agent : m_agents) {
        taken.append(agent.id);
    }

    AiAgentDialog dialog(m_host, {}, taken, m_plugin.connections(), this);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<AiAgent> updated = m_agents;
    updated.append(dialog.agent());
    persist(updated);
}

void AiAgentSettingsView::editAgent() {
    const int row = selectedRow();

    if (row < 0 || row >= static_cast<int>(m_agents.size())) {
        return;
    }

    QStringList taken;

    for (int index = 0; index < static_cast<int>(m_agents.size()); ++index) {
        if (index != row) {
            taken.append(m_agents.at(index).id);
        }
    }

    AiAgentDialog dialog(m_host, m_agents.at(row), taken, m_plugin.connections(), this);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<AiAgent> updated = m_agents;
    updated[row] = dialog.agent();
    persist(updated);
}

// Removing an agent stops the tasks it was handed, so the confirmation says how many it takes with it.
void AiAgentSettingsView::removeAgent() {
    const int row = selectedRow();

    if (row < 0 || row >= static_cast<int>(m_agents.size())) {
        return;
    }

    const QString agentId = m_agents.at(row).id;
    int assigned = 0;

    for (const auto& task : m_plugin.tasks()) {
        if (task.agentId == agentId) {
            ++assigned;
        }
    }

    const QString detail = assigned == 0 ? m_agents.at(row).name : m_host.translate(QStringLiteral("ai.agent.remove-detail")).arg(QString::number(assigned));

    if (!m_host.confirm(this, m_host.translate(QStringLiteral("ai.agent.remove-title")), m_host.translate(QStringLiteral("ai.agent.remove-message")), detail, m_host.translate(QStringLiteral("ai.agent.remove")), true)) {
        return;
    }

    QVector<AiAgent> updated = m_agents;
    updated.removeAt(row);
    persist(updated);
}

void AiAgentSettingsView::persist(const QVector<AiAgent>& agents) {
    auto future = m_plugin.saveAgents(agents);
    // clang-format off
    const auto applied = [this, agents](Result<void> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), m_host.translate(QStringLiteral("ai.error.agent-save")), AlertSeverity::Error); return; } m_agents = agents; rebuild(); };
    // clang-format on
    future.then(this, applied);
}

} // namespace workpane::plugins::ai
