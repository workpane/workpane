#include "AiConnectionSettingsView.h"

#include "AiConnectionDialog.h"
#include "AiPlugin.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace workpane::plugins::ai {

AiConnectionSettingsView::AiConnectionSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_host(host), m_connections(plugin.connections()) {
    setObjectName(QStringLiteral("aiConnectionSettings"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const auto [page, layout] = ui::Components::settingsSectionPage(this);
    auto* form = ui::Components::settingsForm();

    m_defaultConnection = new ui::ComboBox(m_host.theme(), page);
    m_defaultConnection->setObjectName(QStringLiteral("aiDefaultConnection"));

    ui::Components::addSettingsRow(form, m_host.translate(QStringLiteral("ai.settings.default-connection")), m_defaultConnection);
    layout->addLayout(form);

    const auto [actions, actionsLayout] = ui::Components::settingsActionRow(page);
    auto* add = ui::Components::toolButton(ui::IconName::Add, m_host.theme(), m_host.translate(QStringLiteral("ai.connection.add")), actions);
    add->setObjectName(QStringLiteral("aiConnectionAdd"));
    auto* edit = ui::Components::toolButton(ui::IconName::Edit, m_host.theme(), m_host.translate(QStringLiteral("ai.connection.edit")), actions);
    edit->setObjectName(QStringLiteral("aiConnectionEdit"));
    auto* remove = ui::Components::toolButton(ui::IconName::Close, m_host.theme(), m_host.translate(QStringLiteral("ai.connection.remove")), actions);
    remove->setObjectName(QStringLiteral("aiConnectionRemove"));
    actionsLayout->addWidget(add);
    actionsLayout->addWidget(edit);
    actionsLayout->addWidget(remove);
    actionsLayout->addStretch(1);
    layout->addWidget(actions);

    const QStringList headers{m_host.translate(QStringLiteral("ai.connection.display-name")), m_host.translate(QStringLiteral("ai.settings.provider")), m_host.translate(QStringLiteral("ai.settings.model"))};
    m_grid = ui::Components::dataGrid(headers, page);
    m_grid->setObjectName(QStringLiteral("aiConnectionGrid"));
    layout->addWidget(m_grid, 1);

    m_empty = ui::Components::emptyStateLabel(m_host.translate(QStringLiteral("ai.connection.empty")), page);
    m_empty->setObjectName(QStringLiteral("aiConnectionEmpty"));
    layout->addWidget(m_empty, 1);

    // clang-format off
    connect(add, &QToolButton::clicked, this, [this]() { addConnection(); });
    connect(edit, &QToolButton::clicked, this, [this]() { editConnection(); });
    connect(remove, &QToolButton::clicked, this, [this]() { removeConnection(); });
    connect(m_grid, &QTableWidget::doubleClicked, this, [this]() { editConnection(); });
    connect(m_defaultConnection, &QComboBox::currentIndexChanged, this, [this]() { if (!m_loading) { persist(m_connections, selectedDefaultKey()); } });
    // clang-format on

    root->addWidget(page, 1);
    rebuild();
}

int AiConnectionSettingsView::selectedRow() const {
    const auto rows = m_grid->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

QStringList AiConnectionSettingsView::takenKeys(int excludedRow) const {
    QStringList keys;

    for (int index = 0; index < static_cast<int>(m_connections.size()); ++index) {
        if (index != excludedRow) {
            keys.append(ModelConnections::connectionKey(m_connections.at(index)));
        }
    }

    return keys;
}

QString AiConnectionSettingsView::selectedDefaultKey() const {
    return m_defaultConnection->currentData().toString();
}

void AiConnectionSettingsView::rebuild() {
    m_loading = true;
    m_grid->setRowCount(static_cast<int>(m_connections.size()));

    for (int row = 0; row < static_cast<int>(m_connections.size()); ++row) {
        const ModelConnection& connection = m_connections.at(row);
        const ProviderDescriptor* descriptor = ProviderCatalog::findProvider(connection.providerId);
        const QString provider = descriptor != nullptr ? m_host.translate(descriptor->titleKey) : connection.providerId;
        m_grid->setItem(row, 0, new QTableWidgetItem(ModelConnections::connectionLabel(connection)));
        m_grid->setItem(row, 1, new QTableWidgetItem(provider));
        m_grid->setItem(row, 2, new QTableWidgetItem(connection.modelId));
    }

    const QString previous = m_plugin.defaultConnectionKey();
    m_defaultConnection->clear();

    for (const auto& connection : m_connections) {
        m_defaultConnection->addItem(ModelConnections::connectionLabel(connection), ModelConnections::connectionKey(connection));
    }

    ui::Components::sortComboBoxItems(m_defaultConnection);
    m_defaultConnection->setCurrentIndex(std::max(0, m_defaultConnection->findData(previous)));
    m_defaultConnection->setEnabled(!m_connections.isEmpty());

    m_grid->setVisible(!m_connections.isEmpty());
    m_empty->setVisible(m_connections.isEmpty());
    m_loading = false;
}

void AiConnectionSettingsView::addConnection() {
    AiConnectionDialog dialog(m_host, {}, takenKeys(-1), this);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<ModelConnection> updated = m_connections;
    updated.append(dialog.connection());
    persist(updated, m_connections.isEmpty() ? ModelConnections::connectionKey(dialog.connection()) : selectedDefaultKey());
}

void AiConnectionSettingsView::editConnection() {
    const int row = selectedRow();

    if (row < 0 || row >= static_cast<int>(m_connections.size())) {
        return;
    }

    const QString previousKey = ModelConnections::connectionKey(m_connections.at(row));
    AiConnectionDialog dialog(m_host, m_connections.at(row), takenKeys(row), this);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<ModelConnection> updated = m_connections;
    updated[row] = dialog.connection();
    auto future = m_plugin.replaceConnection(previousKey, dialog.connection());
    // clang-format off
    future.then(this, [this, updated](Result<void> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), m_host.translate(QStringLiteral("ai.error.connection-save")), AlertSeverity::Error); return; } m_connections = updated; rebuild(); });
    // clang-format on
}

void AiConnectionSettingsView::removeConnection() {
    const int row = selectedRow();

    if (row < 0 || row >= static_cast<int>(m_connections.size())) {
        return;
    }
    if (!m_host.confirm(this, m_host.translate(QStringLiteral("ai.connection.remove-title")), m_host.translate(QStringLiteral("ai.connection.remove-message")), ModelConnections::connectionLabel(m_connections.at(row)), m_host.translate(QStringLiteral("ai.connection.remove")), true)) {
        return;
    }

    QVector<ModelConnection> updated = m_connections;
    const QString removedKey = ModelConnections::connectionKey(updated.at(row));
    updated.removeAt(row);
    const QString defaultKey = m_plugin.defaultConnectionKey() == removedKey ? (updated.isEmpty() ? QString{} : ModelConnections::connectionKey(updated.first())) : selectedDefaultKey();
    persist(updated, defaultKey);
}

void AiConnectionSettingsView::persist(const QVector<ModelConnection>& connections, const QString& defaultKey) {
    auto future = m_plugin.saveConnections(connections, defaultKey);
    // clang-format off
    future.then(this, [this, connections](Result<void> result) { if (!result.hasValue()) { const QString reason = result.error().code == QStringLiteral("ai_connection_in_use") ? m_host.translate(QStringLiteral("ai.error.connection-in-use")).arg(result.error().detail) : m_host.translate(QStringLiteral("ai.error.connection-save")); m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), reason, AlertSeverity::Error); return; } m_connections = connections; rebuild(); });
    // clang-format on
}

} // namespace workpane::plugins::ai
