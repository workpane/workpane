#include "AiMcpSettingsView.h"

#include "AiPlugin.h"
#include "AiProviderCatalog.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
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
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace workpane::plugins::ai {

constexpr int dialogMinimumWidth = 620;

class AiMcpSettingsViewHelper final {
  public:
    static const QRegularExpression& identifierPattern();
    static bool isWebAddress(const QString& address);
};

const QRegularExpression& AiMcpSettingsViewHelper::identifierPattern() {
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9-]{0,31}$"));
    return pattern;
}

bool AiMcpSettingsViewHelper::isWebAddress(const QString& address) {
    const QUrl parsed(address);
    return parsed.isValid() && (parsed.scheme() == QStringLiteral("http") || parsed.scheme() == QStringLiteral("https"));
}

AiMcpServerDialog::AiMcpServerDialog(PluginHost& host, agent::mcp::McpServerDescriptor server, QStringList takenIdentifiers, QWidget* parent) : QDialog(parent), m_host(host), m_takenIdentifiers(std::move(takenIdentifiers)) {
    setObjectName(QStringLiteral("aiMcpServerDialog"));
    setWindowTitle(m_host.translate(server.id.isEmpty() ? QStringLiteral("ai.mcp.add") : QStringLiteral("ai.mcp.edit")));
    setMinimumWidth(dialogMinimumWidth);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(12);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_identifier = new QLineEdit(server.id, this);
    m_identifier->setObjectName(QStringLiteral("aiMcpIdentifier"));
    m_identifier->setPlaceholderText(m_host.translate(QStringLiteral("ai.mcp.identifier-placeholder")));

    m_transport = new ui::ComboBox(m_host.theme(), this);
    m_transport->setObjectName(QStringLiteral("aiMcpTransport"));
    m_transport->addItem(m_host.translate(QStringLiteral("ai.mcp.transport-stdio")), QStringLiteral("stdio"));
    m_transport->addItem(m_host.translate(QStringLiteral("ai.mcp.transport-http")), QStringLiteral("http"));
    ui::Components::sortComboBoxItems(m_transport);
    m_transport->setCurrentIndex(m_transport->findData(server.transport == agent::mcp::McpTransport::Http ? QStringLiteral("http") : QStringLiteral("stdio")));

    m_command = new QLineEdit(server.command, this);
    m_command->setObjectName(QStringLiteral("aiMcpCommand"));
    m_arguments = new QLineEdit(server.arguments.join(QLatin1Char(' ')), this);
    m_arguments->setObjectName(QStringLiteral("aiMcpArguments"));
    m_workdir = new QLineEdit(server.workdir, this);
    m_workdir->setObjectName(QStringLiteral("aiMcpWorkdir"));
    m_address = new QLineEdit(server.url, this);
    m_address->setObjectName(QStringLiteral("aiMcpAddress"));

    // clang-format off
    const auto confirmReveal = [this]() { return m_host.confirm(this, m_host.translate(QStringLiteral("ai.settings.reveal-title")), m_host.translate(QStringLiteral("ai.settings.reveal-message")), m_host.translate(QStringLiteral("ai.settings.reveal-detail")), m_host.translate(QStringLiteral("ai.settings.reveal-action")), false); };
    // clang-format on
    m_apiKey = new ui::SecretField(m_host.theme(), m_host.translate(QStringLiteral("ai.settings.api-key-placeholder")), confirmReveal, this);
    m_apiKey->setObjectName(QStringLiteral("aiMcpApiKey"));
    m_apiKey->setValue(server.apiKey);

    m_roots = new ui::TextField(m_host.translate(QStringLiteral("ai.mcp.roots-placeholder")), this);
    m_roots->setObjectName(QStringLiteral("aiMcpRoots"));
    m_roots->setPlainText(server.roots.join(QLatin1Char('\n')));

    m_sampling = new QCheckBox(this);
    m_sampling->setObjectName(QStringLiteral("aiMcpSampling"));
    m_sampling->setChecked(server.samplingEnabled);

    m_samplingTokens = new QSpinBox(this);
    m_samplingTokens->setObjectName(QStringLiteral("aiMcpSamplingTokens"));
    m_samplingTokens->setRange(0, ProviderCatalog::aiLimits().maximumSamplingTokens);
    m_samplingTokens->setValue(server.samplingMaximumTokens);

    form->addRow(m_host.translate(QStringLiteral("ai.mcp.identifier")), m_identifier);
    form->addRow(m_host.translate(QStringLiteral("ai.mcp.transport")), m_transport);
    form->addRow(m_host.translate(QStringLiteral("ai.mcp.command")), m_command);
    form->addRow(m_host.translate(QStringLiteral("ai.mcp.arguments")), m_arguments);
    form->addRow(m_host.translate(QStringLiteral("ai.mcp.workdir")), m_workdir);
    form->addRow(m_host.translate(QStringLiteral("ai.mcp.address")), m_address);
    form->addRow(m_host.translate(QStringLiteral("ai.mcp.api-key")), m_apiKey);
    form->addRow(m_host.translate(QStringLiteral("ai.mcp.roots")), m_roots);
    form->addRow(m_host.translate(QStringLiteral("ai.mcp.sampling")), m_sampling);
    form->addRow(m_host.translate(QStringLiteral("ai.mcp.sampling-tokens")), ui::Components::stepperRow(m_samplingTokens, m_host.theme(), this));
    layout->addLayout(form);

    m_validation = new QLabel(this);
    m_validation->setObjectName(QStringLiteral("aiTaskValidation"));
    m_validation->setWordWrap(true);
    m_validation->hide();
    layout->addWidget(m_validation);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primaryButton"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_transport, &QComboBox::currentIndexChanged, this, &AiMcpServerDialog::applyTransport);
    applyTransport();
}

void AiMcpServerDialog::applyTransport() {
    const bool stdio = m_transport->currentData().toString() == QStringLiteral("stdio");
    m_command->setEnabled(stdio);
    m_arguments->setEnabled(stdio);
    m_workdir->setEnabled(stdio);
    m_address->setEnabled(!stdio);
    m_apiKey->setEnabled(!stdio);
}

agent::mcp::McpServerDescriptor AiMcpServerDialog::server() const {
    agent::mcp::McpServerDescriptor server;
    server.id = m_identifier->text().trimmed();
    server.transport = m_transport->currentData().toString() == QStringLiteral("http") ? agent::mcp::McpTransport::Http : agent::mcp::McpTransport::Stdio;
    server.command = m_command->text().trimmed();
    server.arguments = m_arguments->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    server.workdir = m_workdir->text().trimmed();
    server.url = m_address->text().trimmed();
    server.apiKey = m_apiKey->value();
    server.roots = m_roots->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    server.samplingEnabled = m_sampling->isChecked();
    server.samplingMaximumTokens = m_samplingTokens->value();
    return server;
}

void AiMcpServerDialog::accept() {
    const agent::mcp::McpServerDescriptor candidate = server();
    QString message;

    if (!AiMcpSettingsViewHelper::identifierPattern().match(candidate.id).hasMatch()) {
        message = m_host.translate(QStringLiteral("ai.validation.mcp-identifier"));
    } else if (m_takenIdentifiers.contains(candidate.id)) {
        message = m_host.translate(QStringLiteral("ai.validation.mcp-duplicate"));
    } else if (candidate.transport == agent::mcp::McpTransport::Stdio && candidate.command.isEmpty()) {
        message = m_host.translate(QStringLiteral("ai.validation.mcp-command"));
    } else if (candidate.transport == agent::mcp::McpTransport::Http && !AiMcpSettingsViewHelper::isWebAddress(candidate.url)) {
        message = m_host.translate(QStringLiteral("ai.validation.mcp-address"));
    }

    if (!message.isEmpty()) {
        m_validation->setText(message);
        m_validation->show();
        ui::Components::growDialogToContents(this);
        return;
    }

    QDialog::accept();
}

AiMcpSettingsView::AiMcpSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_host(host), m_servers(plugin.mcpServers()) {
    setObjectName(QStringLiteral("aiMcpSettings"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const auto [page, layout] = ui::Components::settingsSectionPage(this);

    const auto [actions, actionsLayout] = ui::Components::settingsActionRow(page);
    auto* add = ui::Components::toolButton(ui::IconName::Add, m_host.theme(), m_host.translate(QStringLiteral("ai.mcp.add")), actions);
    add->setObjectName(QStringLiteral("aiMcpAdd"));
    auto* edit = ui::Components::toolButton(ui::IconName::Edit, m_host.theme(), m_host.translate(QStringLiteral("ai.mcp.edit")), actions);
    edit->setObjectName(QStringLiteral("aiMcpEdit"));
    auto* remove = ui::Components::toolButton(ui::IconName::Close, m_host.theme(), m_host.translate(QStringLiteral("ai.mcp.remove")), actions);
    remove->setObjectName(QStringLiteral("aiMcpRemove"));
    actionsLayout->addWidget(add);
    actionsLayout->addWidget(edit);
    actionsLayout->addWidget(remove);
    actionsLayout->addStretch(1);
    layout->addWidget(actions);

    const QStringList headers{m_host.translate(QStringLiteral("ai.mcp.identifier")), m_host.translate(QStringLiteral("ai.mcp.transport")), m_host.translate(QStringLiteral("ai.mcp.target")), m_host.translate(QStringLiteral("ai.mcp.tools"))};
    m_grid = ui::Components::dataGrid(headers, page);
    m_grid->setObjectName(QStringLiteral("aiMcpGrid"));
    layout->addWidget(m_grid, 1);

    m_empty = ui::Components::emptyStateLabel(m_host.translate(QStringLiteral("ai.mcp.empty")), page);
    m_empty->setObjectName(QStringLiteral("aiMcpEmpty"));
    layout->addWidget(m_empty, 1);

    connect(add, &QToolButton::clicked, this, &AiMcpSettingsView::addServer);
    connect(edit, &QToolButton::clicked, this, &AiMcpSettingsView::editServer);
    connect(remove, &QToolButton::clicked, this, &AiMcpSettingsView::removeServer);
    // clang-format off
    connect(m_grid, &QTableWidget::doubleClicked, this, [this]() { editServer(); });
    // clang-format on

    root->addWidget(page, 1);
    rebuild();
}

int AiMcpSettingsView::selectedRow() const {
    const auto rows = m_grid->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void AiMcpSettingsView::rebuild() {
    m_grid->setRowCount(static_cast<int>(m_servers.size()));

    for (int row = 0; row < static_cast<int>(m_servers.size()); ++row) {
        const agent::mcp::McpServerDescriptor& server = m_servers.at(row);
        const bool stdio = server.transport == agent::mcp::McpTransport::Stdio;
        const QString transport = m_host.translate(stdio ? QStringLiteral("ai.mcp.transport-stdio") : QStringLiteral("ai.mcp.transport-http"));
        const QString target = stdio ? QStringList{server.command, server.arguments.join(QLatin1Char(' '))}.join(QLatin1Char(' ')).trimmed() : server.url;
        m_grid->setItem(row, 0, new QTableWidgetItem(server.id));
        m_grid->setItem(row, 1, new QTableWidgetItem(transport));
        m_grid->setItem(row, 2, new QTableWidgetItem(target));
        m_grid->setItem(row, 3, new QTableWidgetItem(QString::number(m_plugin.mcpToolCount(server.id))));
    }

    m_grid->setVisible(!m_servers.isEmpty());
    m_empty->setVisible(m_servers.isEmpty());
}

void AiMcpSettingsView::addServer() {
    QStringList taken;

    for (const auto& server : m_servers) {
        taken.append(server.id);
    }

    AiMcpServerDialog dialog(m_host, {}, taken, this);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<agent::mcp::McpServerDescriptor> updated = m_servers;
    updated.append(dialog.server());
    persist(updated);
}

void AiMcpSettingsView::editServer() {
    const int row = selectedRow();

    if (row < 0 || row >= static_cast<int>(m_servers.size())) {
        return;
    }

    QStringList taken;

    for (int index = 0; index < static_cast<int>(m_servers.size()); ++index) {
        if (index != row) {
            taken.append(m_servers.at(index).id);
        }
    }

    AiMcpServerDialog dialog(m_host, m_servers.at(row), taken, this);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<agent::mcp::McpServerDescriptor> updated = m_servers;
    updated[row] = dialog.server();
    persist(updated);
}

void AiMcpSettingsView::removeServer() {
    const int row = selectedRow();

    if (row < 0 || row >= static_cast<int>(m_servers.size())) {
        return;
    }
    if (!m_host.confirm(this, m_host.translate(QStringLiteral("ai.mcp.remove-title")), m_host.translate(QStringLiteral("ai.mcp.remove-message")), m_servers.at(row).id, m_host.translate(QStringLiteral("ai.mcp.remove")), true)) {
        return;
    }

    QVector<agent::mcp::McpServerDescriptor> updated = m_servers;
    updated.removeAt(row);
    persist(updated);
}

void AiMcpSettingsView::persist(const QVector<agent::mcp::McpServerDescriptor>& servers) {
    auto future = m_plugin.saveMcpServers(servers);
    // clang-format off
    future.then(this, [this, servers](Result<void> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), m_host.translate(QStringLiteral("ai.error.mcp-save")), AlertSeverity::Error); return; } m_servers = servers; rebuild(); });
    // clang-format on
}

} // namespace workpane::plugins::ai
