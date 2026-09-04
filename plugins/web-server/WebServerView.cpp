#include "WebServerView.h"

#include "persistence/StoredValues.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace workpane::plugins::webserver {

constexpr int maximumVisibleRequests = 500;
constexpr int maximumRequestBatch = 128;
constexpr int requestRefreshIntervalMilliseconds = 250;
constexpr int tableHeaderHeight = 34;
constexpr int tableRowHeight = 34;

class ServerSplitterHandle final : public QSplitterHandle {
  public:
    ServerSplitterHandle(Qt::Orientation orientation, const ui::Theme& theme, QSplitter* parent) : QSplitterHandle(orientation, parent), m_theme(theme) {
        setMouseTracking(true);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), m_theme.color(ui::ThemeColor::Window));
        painter.setPen(m_theme.color(underMouse() ? ui::ThemeColor::Accent : ui::ThemeColor::Border));
        const int center = rect().center().y();
        painter.drawLine(rect().left(), center, rect().right(), center);
    }

  private:
    const ui::Theme& m_theme;
};

class ServerSplitter final : public QSplitter {
  public:
    ServerSplitter(const ui::Theme& theme, QWidget* parent) : QSplitter(Qt::Vertical, parent), m_theme(theme) {}

  protected:
    [[nodiscard]] QSplitterHandle* createHandle() override {
        return new ServerSplitterHandle(orientation(), m_theme, this);
    }

  private:
    const ui::Theme& m_theme;
};

class WebServerViewHelper final {
  public:
    static QUrl serverUrl(const QString& host, quint16 port);
    static QToolButton* actionButton(ui::IconName iconName, const ui::Theme& theme, ui::ThemeColor role, const QString& tooltip, const QString& serverId, QWidget* parent);
};

QUrl WebServerViewHelper::serverUrl(const QString& host, quint16 port) {
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(host);
    url.setPort(port);
    url.setPath(QStringLiteral("/"));
    return url;
}

QToolButton* WebServerViewHelper::actionButton(ui::IconName iconName, const ui::Theme& theme, ui::ThemeColor role, const QString& tooltip, const QString& serverId, QWidget* parent) {
    QToolButton* button = ui::Components::rowActionButton(iconName, role, theme, tooltip, parent);
    button->setObjectName(QStringLiteral("serverActionButton"));
    button->setProperty("serverId", serverId);
    return button;
}

class RequestTableModel final : public QAbstractTableModel {
  public:
    RequestTableModel(plugins::PluginHost& host, QObject* parent = nullptr) : QAbstractTableModel(parent), m_host(host) {}

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
    }

    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : 7;
    }

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= m_entries.size()) {
            return {};
        }

        const auto& entry = m_entries.at(index.row());

        if (role == Qt::ForegroundRole && index.column() == 1) {
            return m_host.theme().color(entry.status < 400 ? ui::ThemeColor::Success : ui::ThemeColor::Danger);
        }
        if (role == Qt::TextAlignmentRole) {
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
        if (role == Qt::ToolTipRole && index.column() == 3) {
            return entry.path;
        }
        if (role != Qt::DisplayRole) {
            return {};
        }

        switch (index.column()) {
        case 0:
            return entry.localDateTime;
        case 1:
            return QString::number(entry.status);
        case 2:
            return entry.method;
        case 3:
            return entry.path;
        case 4:
            return m_host.translate(QStringLiteral("web-server.requests.milliseconds")).arg(entry.durationMilliseconds);
        case 5:
            return QLocale::system().formattedDataSize(entry.responseBytes);
        case 6:
            return entry.remoteAddress;
        default:
            return {};
        }
    }

    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return {};
        }

        switch (section) {
        case 0:
            return m_host.translate(QStringLiteral("web-server.requests.date-time"));
        case 1:
            return m_host.translate(QStringLiteral("web-server.requests.status"));
        case 2:
            return m_host.translate(QStringLiteral("web-server.requests.method"));
        case 3:
            return m_host.translate(QStringLiteral("web-server.requests.path"));
        case 4:
            return m_host.translate(QStringLiteral("web-server.requests.duration"));
        case 5:
            return m_host.translate(QStringLiteral("web-server.requests.response-size"));
        case 6:
            return m_host.translate(QStringLiteral("web-server.requests.remote-address"));
        default:
            return {};
        }
    }

    void append(const QVariantList& values) {
        if (values.isEmpty()) {
            return;
        }

        QVector<Entry> incoming;
        incoming.reserve(values.size());

        for (const auto& value : values) {
            const QVariantMap entry = value.toMap();
            const QDateTime timestamp = persistence::StoredValues::parseStoredTimestamp(entry.value("timestamp"));
            incoming.append({ui::Components::localTimestamp(timestamp), entry.value("status").toInt(), entry.value("method").toString(), entry.value("path").toString(), entry.value("durationMs").toLongLong(), entry.value("responseBytes").toLongLong(), entry.value("remoteAddress").toString()});
        }

        const int incomingCount = static_cast<int>(incoming.size());
        beginInsertRows({}, 0, incomingCount - 1);

        for (auto& entry : incoming) {
            m_entries.prepend(std::move(entry));
        }

        endInsertRows();

        const int overflow = std::max(0, static_cast<int>(m_entries.size()) - maximumVisibleRequests);

        if (overflow == 0) {
            return;
        }

        const int firstRemovedRow = static_cast<int>(m_entries.size()) - overflow;
        beginRemoveRows({}, firstRemovedRow, static_cast<int>(m_entries.size()) - 1);
        m_entries.remove(firstRemovedRow, overflow);
        endRemoveRows();
    }

    void clear() {
        if (m_entries.isEmpty()) {
            return;
        }

        beginResetModel();
        m_entries.clear();
        endResetModel();
    }

  private:
    struct Entry final {
        QString localDateTime;
        int status{};
        QString method;
        QString path;
        qint64 durationMilliseconds{};
        qint64 responseBytes{};
        QString remoteAddress;
    };

    plugins::PluginHost& m_host;
    QVector<Entry> m_entries;
};

WebServerDialog::WebServerDialog(QString serverId, QString terminalId, QString initialRoot, plugins::webserver::WebServerPlugin& plugin, QWidget* parent) : QDialog(parent), m_serverId(std::move(serverId)), m_terminalId(std::move(terminalId)), m_initialRoot(std::move(initialRoot)), m_plugin(plugin) {
    setObjectName(QStringLiteral("webServerDialog"));
    setWindowTitle(m_plugin.host().translate(QStringLiteral("web-server.dialog.title")));
    setModal(true);
    setMinimumWidth(580);

    const QVariantMap terminal = m_plugin.terminalData(m_terminalId);
    const QString initialName = m_plugin.webServerConfigured(m_serverId) ? m_plugin.webServerName(m_serverId) : m_initialRoot.isEmpty() ? terminal.value(QStringLiteral("name")).toString() : QFileInfo(m_initialRoot).fileName();
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(14);
    root->setSizeConstraint(QLayout::SetMinimumSize);

    auto* heading = new QLabel(initialName.isEmpty() ? m_plugin.host().translate(QStringLiteral("web-server.dialog.new-heading")) : m_plugin.host().translate(QStringLiteral("web-server.dialog.heading")).arg(initialName), this);
    heading->setObjectName(QStringLiteral("dialogTitle"));
    auto* description = new QLabel(m_plugin.host().translate(QStringLiteral("web-server.dialog.description")), this);
    description->setObjectName(QStringLiteral("mutedLabel"));
    root->addWidget(heading);
    root->addWidget(description);

    auto* form = new QWidget(this);
    form->setObjectName(QStringLiteral("webServerConfiguration"));
    auto* formLayout = new QGridLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setHorizontalSpacing(10);
    formLayout->setVerticalSpacing(10);

    auto* nameLabel = new QLabel(m_plugin.host().translate(QStringLiteral("web-server.dialog.name")), form);
    m_nameEdit = new QLineEdit(form);
    m_nameEdit->setClearButtonEnabled(true);
    m_nameEdit->setPlaceholderText(m_plugin.host().translate(QStringLiteral("web-server.dialog.name-placeholder")));

    auto* rootLabel = new QLabel(m_plugin.host().translate(QStringLiteral("web-server.dialog.document-root")), form);
    m_rootEdit = new QLineEdit(form);
    m_rootEdit->setClearButtonEnabled(true);
    m_rootEdit->setPlaceholderText(m_plugin.host().translate(QStringLiteral("web-server.dialog.absolute-path")));
    m_rootButton = new QToolButton(form);
    m_rootButton->setObjectName(QStringLiteral("fieldActionButton"));
    m_rootButton->setAutoRaise(true);
    m_rootButton->setIcon(ui::IconCatalog::icon(ui::IconName::Search, m_plugin.host().theme()));
    m_rootButton->setToolTip(m_plugin.host().translate(QStringLiteral("web-server.dialog.choose-root")));
    m_rootButton->setFixedSize(30, 30);

    auto* hostLabel = new QLabel(m_plugin.host().translate(QStringLiteral("web-server.dialog.bind-host")), form);
    m_hostEdit = new QLineEdit(form);
    m_hostEdit->setPlaceholderText(QStringLiteral("127.0.0.1"));
    auto* portLabel = new QLabel(m_plugin.host().translate(QStringLiteral("web-server.dialog.port")), form);
    m_portSpin = new QSpinBox(form);
    m_portSpin->setObjectName(QStringLiteral("webServerPortField"));
    m_portSpin->setRange(1, 65535);
    m_portSpin->setFixedWidth(130);
    auto* portRow = ui::Components::stepperRow(m_portSpin, m_plugin.host().theme(), form);

    formLayout->addWidget(nameLabel, 0, 0);
    formLayout->addWidget(m_nameEdit, 0, 1, 1, 4);
    formLayout->addWidget(rootLabel, 1, 0);
    formLayout->addWidget(m_rootEdit, 1, 1, 1, 3);
    formLayout->addWidget(m_rootButton, 1, 4);
    formLayout->addWidget(hostLabel, 2, 0);
    formLayout->addWidget(m_hostEdit, 2, 1);
    formLayout->addWidget(portLabel, 2, 2);
    formLayout->addWidget(portRow, 2, 3);
    formLayout->setColumnStretch(1, 1);
    root->addWidget(form);

    auto* status = new QWidget(this);
    status->setObjectName(QStringLiteral("webServerDialogStatus"));
    auto* statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(10, 8, 10, 8);
    statusLayout->setSpacing(8);
    m_statusIndicator = new ui::StatusIndicator(status);
    m_statusLabel = new QLabel(status);
    m_addressLabel = new QLabel(status);
    m_addressLabel->setObjectName(QStringLiteral("mutedLabel"));
    statusLayout->addWidget(m_statusIndicator);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addWidget(m_addressLabel, 1);
    root->addWidget(status);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("webServerError"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    root->addWidget(m_errorLabel);

    auto* controls = new QHBoxLayout();
    controls->setSpacing(8);
    auto* closeButton = new QPushButton(m_plugin.host().translate(QStringLiteral("web-server.dialog.close")), this);
    m_startButton = new QPushButton(ui::IconCatalog::primaryIcon(ui::IconName::Start, m_plugin.host().theme()), m_plugin.host().translate(QStringLiteral("web-server.dialog.start")), this);
    m_startButton->setObjectName(QStringLiteral("webServerStartButton"));
    m_startButton->setProperty("primary", true);
    m_openButton = new QPushButton(ui::IconCatalog::icon(ui::IconName::WebServer, m_plugin.host().theme()), m_plugin.host().translate(QStringLiteral("web-server.dialog.open")), this);
    m_editButton = new QPushButton(ui::IconCatalog::icon(ui::IconName::Edit, m_plugin.host().theme()), m_plugin.host().translate(QStringLiteral("web-server.dialog.edit")), this);
    m_stopButton = new QPushButton(ui::IconCatalog::icon(ui::IconName::Stop, m_plugin.host().theme().color(ui::ThemeColor::Danger)), m_plugin.host().translate(QStringLiteral("web-server.dialog.stop")), this);
    m_stopButton->setObjectName(QStringLiteral("webServerStopButton"));
    controls->addStretch(1);
    controls->addWidget(closeButton);
    controls->addWidget(m_editButton);
    controls->addWidget(m_openButton);
    controls->addWidget(m_stopButton);
    controls->addWidget(m_startButton);
    root->addLayout(controls);

    connect(m_rootButton, &QToolButton::clicked, this, &WebServerDialog::chooseRoot);
    connect(m_startButton, &QPushButton::clicked, this, &WebServerDialog::startServer);
    connect(m_stopButton, &QPushButton::clicked, this, &WebServerDialog::stopServer);
    connect(m_editButton, &QPushButton::clicked, this, &WebServerDialog::editServer);
    connect(m_openButton, &QPushButton::clicked, this, &WebServerDialog::openServer);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_hostEdit, &QLineEdit::textChanged, this, &WebServerDialog::updateAddressPreview);
    connect(m_portSpin, &QSpinBox::valueChanged, this, &WebServerDialog::updateAddressPreview);
    connect(&m_plugin, &plugins::webserver::WebServerPlugin::webServerChanged, this, &WebServerDialog::refreshState);

    loadConfiguration();
    refreshState();
}

void WebServerDialog::chooseRoot() {
    const QString directory = QFileDialog::getExistingDirectory(this, m_plugin.host().translate(QStringLiteral("web-server.dialog.choose-root-title")), m_rootEdit->text().trimmed(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!directory.isEmpty()) {
        m_rootEdit->setText(directory);
        if (m_nameEdit->text().trimmed().isEmpty()) {
            m_nameEdit->setText(QFileInfo(directory).fileName());
        }
    }
}

void WebServerDialog::startServer() {
    auto future = m_plugin.configureAndStartWebServer(m_serverId, m_nameEdit->text().trimmed(), m_rootEdit->text().trimmed(), m_hostEdit->text().trimmed(), m_portSpin->value(), m_terminalId);
    // clang-format off
    future.then(this, [this](Result<void> result) {
        if (!result.hasValue()) {
            showError(result.error());
            refreshState();
            return;
        }
        hideError();
        // A server that answered is configured and running, so the form has nothing left to ask.
        accept();
    });
    // clang-format on
}

void WebServerDialog::stopServer() {
    m_plugin.stopWebServer(m_serverId);
    hideError();
}

void WebServerDialog::editServer() {
    const bool confirmed = m_plugin.host().confirm(this, m_plugin.host().translate(QStringLiteral("web-server.dialog.edit-title")), m_plugin.host().translate(QStringLiteral("web-server.dialog.edit-message")), m_plugin.host().translate(QStringLiteral("web-server.dialog.edit-detail")), m_plugin.host().translate(QStringLiteral("web-server.dialog.stop-edit")), true);

    if (!confirmed) {
        return;
    }

    m_plugin.stopWebServer(m_serverId);
    m_rootEdit->setFocus(Qt::OtherFocusReason);
}

void WebServerDialog::openServer() {
    if (!m_plugin.openWebServer(m_serverId)) {
        showError({"web_server_open_failed", m_plugin.host().translate(QStringLiteral("web-server.error.open")), {}});
    }
}

void WebServerDialog::refreshState(const QString& changedServerId) {
    if (!changedServerId.isEmpty() && changedServerId != m_serverId) {
        return;
    }

    const bool running = m_plugin.webServerRunning(m_serverId);
    const bool pending = m_plugin.webServerOperationPending(m_serverId);
    const QColor statusColor = running ? m_plugin.host().theme().color(ui::ThemeColor::Success) : pending ? m_plugin.host().theme().color(ui::ThemeColor::Warning) : m_plugin.host().theme().color(ui::ThemeColor::TextMuted);
    m_statusIndicator->setColor(statusColor);
    m_statusLabel->setText(m_plugin.host().translate(running ? QStringLiteral("web-server.manager.running") : pending ? QStringLiteral("web-server.manager.working") : QStringLiteral("web-server.manager.stopped")));
    updateAddressPreview();

    m_nameEdit->setEnabled(!running && !pending);
    m_rootEdit->setEnabled(!running && !pending);
    m_hostEdit->setEnabled(!running && !pending);
    m_portSpin->setEnabled(!running && !pending);
    m_rootButton->setEnabled(!running && !pending);
    m_startButton->setVisible(!running);
    m_startButton->setEnabled(!pending);
    m_openButton->setVisible(running);
    m_editButton->setVisible(running);
    m_stopButton->setVisible(running || pending);
}

void WebServerDialog::updateAddressPreview() {
    m_addressLabel->setText(WebServerViewHelper::serverUrl(m_hostEdit->text().trimmed(), static_cast<quint16>(m_portSpin->value())).toString());
}

void WebServerDialog::loadConfiguration() {
    if (m_plugin.webServerConfigured(m_serverId)) {
        m_nameEdit->setText(m_plugin.webServerName(m_serverId));
        m_rootEdit->setText(m_plugin.webServerRoot(m_serverId));
        m_hostEdit->setText(m_plugin.webServerHost(m_serverId));
        m_portSpin->setValue(m_plugin.webServerPort(m_serverId));
        return;
    }

    // A folder names the server it is served by, and the port it opens on is the next one no other configuration took.
    if (!m_initialRoot.isEmpty()) {
        m_nameEdit->setText(QFileInfo(m_initialRoot).fileName());
        m_rootEdit->setText(m_initialRoot);
        m_hostEdit->setText(QStringLiteral("127.0.0.1"));
        m_portSpin->setValue(m_plugin.availablePort());
        return;
    }

    const QVariantMap terminal = m_plugin.terminalData(m_terminalId);
    m_nameEdit->setText(terminal.value(QStringLiteral("name")).toString());
    m_rootEdit->setText(terminal.value(QStringLiteral("cwd")).toString());
    m_hostEdit->setText(QStringLiteral("127.0.0.1"));
    m_portSpin->setValue(m_plugin.availablePort());
}

void WebServerDialog::hideError() {
    if (m_errorLabel->isHidden()) {
        return;
    }

    m_errorLabel->hide();
    ui::Components::growDialogToContents(this);
}

void WebServerDialog::showError(const Error& error) {
    m_errorLabel->setText(error.detail.isEmpty() ? error.message : QStringLiteral("%1\n%2").arg(error.message, error.detail));
    m_errorLabel->show();
    ui::Components::growDialogToContents(this);
}

WebServerView::WebServerView(plugins::webserver::WebServerPlugin& plugin, QWidget* parent) : QWidget(parent), m_plugin(plugin) {
    setObjectName(QStringLiteral("webServerView"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new ui::PageHeader(m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("web-server.manager.title")), this);
    auto* terminalButton = new QPushButton(ui::IconCatalog::icon(ui::IconName::Terminal, m_plugin.host().theme()), m_plugin.host().translate(QStringLiteral("web-server.manager.from-terminal")), header);
    terminalButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    terminalButton->setVisible(m_plugin.host().capabilityAvailable(QString::fromLatin1(plugins::terminalSnapshotCapability)));
    auto* createButton = new QPushButton(ui::IconCatalog::primaryIcon(ui::IconName::Add, m_plugin.host().theme()), m_plugin.host().translate(QStringLiteral("web-server.manager.new-server")), header);
    createButton->setObjectName(QStringLiteral("primaryButton"));
    createButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    header->addStretch();
    header->addWidget(terminalButton);
    header->addWidget(createButton);
    root->addWidget(header);

    m_splitter = new ServerSplitter(m_plugin.host().theme(), this);
    m_splitter->setObjectName(QStringLiteral("webServerSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(5);

    auto* serverPane = new QWidget();
    auto* serverPaneLayout = new QVBoxLayout(serverPane);
    serverPaneLayout->setContentsMargins(0, 0, 0, 0);
    serverPaneLayout->setSpacing(0);
    auto* summaryBand = new QWidget(serverPane);
    auto* summaryLayout = new QHBoxLayout(summaryBand);
    summaryLayout->setContentsMargins(18, 12, 18, 10);
    m_summaryLabel = new QLabel(summaryBand);
    m_summaryLabel->setObjectName(QStringLiteral("mutedLabel"));
    summaryLayout->addWidget(m_summaryLabel);
    serverPaneLayout->addWidget(summaryBand);

    m_emptyLabel = ui::Components::emptyStateLabel(m_plugin.host().translate(QStringLiteral("web-server.manager.empty")), serverPane);
    m_emptyLabel->setContentsMargins(18, 12, 18, 12);
    serverPaneLayout->addWidget(m_emptyLabel, 1);

    m_serverTable = ui::Components::dataGrid({m_plugin.host().translate(QStringLiteral("web-server.manager.status")), m_plugin.host().translate(QStringLiteral("web-server.manager.name")), m_plugin.host().translate(QStringLiteral("web-server.manager.address")), m_plugin.host().translate(QStringLiteral("web-server.manager.document-root")), {}}, serverPane);
    m_serverTable->setObjectName(QStringLiteral("webServerTable"));
    m_serverTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_serverTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_serverTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_serverTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_serverTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_serverTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_serverTable->setColumnWidth(4, 132);
    ui::Components::stretchGridColumn(m_serverTable, 3);
    QPalette serverTablePalette = m_serverTable->palette();
    serverTablePalette.setColor(QPalette::Highlight, m_plugin.host().theme().color(ui::ThemeColor::Hover));
    serverTablePalette.setColor(QPalette::HighlightedText, m_plugin.host().theme().color(ui::ThemeColor::Text));
    m_serverTable->setPalette(serverTablePalette);
    serverPaneLayout->addWidget(m_serverTable, 1);

    m_requestPane = new QWidget();
    auto* requestPaneLayout = new QVBoxLayout(m_requestPane);
    requestPaneLayout->setContentsMargins(0, 0, 0, 0);
    requestPaneLayout->setSpacing(0);
    auto* requestHeader = new QWidget(m_requestPane);
    auto* requestHeaderLayout = new QHBoxLayout(requestHeader);
    requestHeaderLayout->setContentsMargins(18, 10, 10, 8);
    m_requestTitle = ui::Components::sectionTitleLabel(m_plugin.host().translate(QStringLiteral("web-server.manager.requests")), requestHeader);
    m_clearRequestsButton = new QPushButton(ui::IconCatalog::icon(ui::IconName::Clear, m_plugin.host().theme()), m_plugin.host().translate(QStringLiteral("web-server.manager.clear-log")), requestHeader);
    m_clearRequestsButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_clearRequestsButton->setEnabled(false);
    requestHeaderLayout->addWidget(m_requestTitle);
    requestHeaderLayout->addStretch(1);
    requestHeaderLayout->addWidget(m_clearRequestsButton);
    requestPaneLayout->addWidget(requestHeader);

    m_requestTable = new QTableView(m_requestPane);
    m_requestTable->setObjectName(QStringLiteral("webServerRequestTable"));
    m_requestModel = new RequestTableModel(m_plugin.host(), m_requestTable);
    m_requestTable->setModel(m_requestModel);
    m_requestTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_requestTable->horizontalHeader()->setFixedHeight(tableHeaderHeight);
    m_requestTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_requestTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_requestTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_requestTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_requestTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_requestTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_requestTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    m_requestTable->setColumnWidth(0, 180);
    m_requestTable->setColumnWidth(1, 90);
    m_requestTable->setColumnWidth(2, 100);
    m_requestTable->setColumnWidth(4, 100);
    m_requestTable->setColumnWidth(5, 110);
    m_requestTable->setColumnWidth(6, 140);
    m_requestTable->verticalHeader()->hide();
    m_requestTable->verticalHeader()->setDefaultSectionSize(tableRowHeight);
    m_requestTable->verticalHeader()->setMinimumSectionSize(tableRowHeight);
    m_requestTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_requestTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_requestTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_requestTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_requestTable->setTextElideMode(Qt::ElideRight);
    m_requestTable->setWordWrap(false);
    m_requestTable->setShowGrid(false);
    requestPaneLayout->addWidget(m_requestTable, 1);

    m_splitter->addWidget(serverPane);
    m_splitter->addWidget(m_requestPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({m_plugin.splitRatio(), 1000 - m_plugin.splitRatio()});
    root->addWidget(m_splitter, 1);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(requestRefreshIntervalMilliseconds);
    m_splitterSaveTimer = new QTimer(this);
    m_splitterSaveTimer->setSingleShot(true);
    m_splitterSaveTimer->setInterval(200);
    connect(createButton, &QPushButton::clicked, this, &WebServerView::createServer);
    connect(terminalButton, &QPushButton::clicked, this, &WebServerView::createServerFromTerminal);
    connect(m_serverTable, &QTableWidget::itemSelectionChanged, this, &WebServerView::selectServer);
    // clang-format off
    connect(m_serverTable, &QTableWidget::doubleClicked, this, [this]() { openServerEditor(selectedServerId()); });
    // clang-format on
    connect(&m_plugin, &plugins::webserver::WebServerPlugin::webServerChanged, this, &WebServerView::refreshInstances);
    connect(&m_plugin, &plugins::webserver::WebServerPlugin::folderRequested, this, &WebServerView::openServerForFolder);
    connect(m_refreshTimer, &QTimer::timeout, this, &WebServerView::refreshRequests);
    connect(m_clearRequestsButton, &QPushButton::clicked, this, &WebServerView::clearRequests);
    connect(m_splitter, &QSplitter::splitterMoved, this, &WebServerView::scheduleSplitterSave);
    connect(m_splitterSaveTimer, &QTimer::timeout, this, &WebServerView::persistSplitterRatio);
    m_refreshTimer->start();
    refreshInstances();
}

WebServerView::~WebServerView() {
    if (!m_splitterSaveTimer->isActive()) {
        return;
    }

    m_splitterSaveTimer->stop();
    persistSplitterRatio();
}

void WebServerView::refreshInstances(const QString& changedServerId) {
    const QString selectedId = selectedServerId();
    const QVariantList servers = m_plugin.configuredWebServers();
    m_serverTable->setRowCount(static_cast<int>(servers.size()));

    int selectedRow = -1;
    int runningCount = 0;

    for (int row = 0; row < static_cast<int>(servers.size()); ++row) {
        const QVariantMap server = servers.at(row).toMap();
        const QString serverId = server.value(QStringLiteral("serverId")).toString();
        const bool running = server.value("running").toBool();
        const bool pending = server.value("pending").toBool();
        runningCount += running ? 1 : 0;

        const QString statusText = m_plugin.host().translate(running ? QStringLiteral("web-server.manager.running") : pending ? QStringLiteral("web-server.manager.working") : QStringLiteral("web-server.manager.stopped"));
        const auto statusColor = running ? ui::ThemeColor::Success : pending ? ui::ThemeColor::Warning : ui::ThemeColor::TextMuted;
        QTableWidgetItem* statusItem = ui::Components::gridStatusItem(statusText, ui::IconName::WebServer, statusColor, m_plugin.host().theme());
        statusItem->setData(Qt::UserRole, serverId);
        m_serverTable->setItem(row, 0, statusItem);
        m_serverTable->setItem(row, 1, new QTableWidgetItem(server.value(QStringLiteral("name")).toString()));
        m_serverTable->setItem(row, 2, new QTableWidgetItem(WebServerViewHelper::serverUrl(server.value("host").toString(), static_cast<quint16>(server.value("port").toUInt())).toString()));
        m_serverTable->setItem(row, 3, new QTableWidgetItem(server.value("root").toString()));

        auto* actions = new QWidget(m_serverTable);
        auto* actionsLayout = new QHBoxLayout(actions);
        actionsLayout->setContentsMargins(4, 0, 4, 0);
        actionsLayout->setSpacing(3);
        actionsLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        if (pending) {
            auto* stop = WebServerViewHelper::actionButton(ui::IconName::Stop, m_plugin.host().theme(), ui::ThemeColor::Danger, m_plugin.host().translate(QStringLiteral("web-server.manager.stop-server")), serverId, actions);
            actionsLayout->addWidget(stop);
            connect(stop, &QToolButton::clicked, this, &WebServerView::stopServer);
        } else if (running) {
            auto* edit = WebServerViewHelper::actionButton(ui::IconName::Edit, m_plugin.host().theme(), ui::ThemeColor::TextMuted, m_plugin.host().translate(QStringLiteral("web-server.dialog.edit")), serverId, actions);
            auto* openExternally = WebServerViewHelper::actionButton(ui::IconName::ExternalLink, m_plugin.host().theme(), ui::ThemeColor::Success, m_plugin.host().translate(QStringLiteral("web-server.manager.open-browser")), serverId, actions);
            auto* openInBrowser = WebServerViewHelper::actionButton(ui::IconName::Browser, m_plugin.host().theme(), ui::ThemeColor::Success, m_plugin.host().translate(QStringLiteral("web-server.manager.open-browser-plugin")), serverId, actions);
            auto* stop = WebServerViewHelper::actionButton(ui::IconName::Stop, m_plugin.host().theme(), ui::ThemeColor::Danger, m_plugin.host().translate(QStringLiteral("web-server.manager.stop-server")), serverId, actions);
            actionsLayout->addWidget(edit);
            actionsLayout->addWidget(openExternally);
            actionsLayout->addWidget(openInBrowser);
            actionsLayout->addWidget(stop);
            connect(edit, &QToolButton::clicked, this, &WebServerView::editServerConfiguration);
            connect(openExternally, &QToolButton::clicked, this, &WebServerView::openServer);
            connect(openInBrowser, &QToolButton::clicked, this, &WebServerView::openServerInBrowser);
            connect(stop, &QToolButton::clicked, this, &WebServerView::stopServer);
        } else {
            auto* edit = WebServerViewHelper::actionButton(ui::IconName::Edit, m_plugin.host().theme(), ui::ThemeColor::TextMuted, m_plugin.host().translate(QStringLiteral("web-server.dialog.edit")), serverId, actions);
            auto* start = WebServerViewHelper::actionButton(ui::IconName::Start, m_plugin.host().theme(), ui::ThemeColor::Success, m_plugin.host().translate(QStringLiteral("web-server.manager.start-server")), serverId, actions);
            auto* remove = WebServerViewHelper::actionButton(ui::IconName::Close, m_plugin.host().theme(), ui::ThemeColor::Danger, m_plugin.host().translate(QStringLiteral("web-server.manager.remove-server")), serverId, actions);
            actionsLayout->addWidget(edit);
            actionsLayout->addWidget(start);
            actionsLayout->addWidget(remove);
            connect(edit, &QToolButton::clicked, this, &WebServerView::editServerConfiguration);
            connect(start, &QToolButton::clicked, this, &WebServerView::startServer);
            connect(remove, &QToolButton::clicked, this, &WebServerView::removeServerConfiguration);
        }
        m_serverTable->setCellWidget(row, 4, actions);
        m_serverTable->setRowHeight(row, 36);
        if (serverId == selectedId) {
            selectedRow = row;
        }
    }

    const bool empty = servers.isEmpty();
    m_emptyLabel->setVisible(empty);
    m_serverTable->setVisible(!empty);
    m_requestPane->setVisible(!empty);
    m_summaryLabel->setText(m_plugin.host().translate(QStringLiteral("web-server.manager.summary")).arg(servers.size()).arg(runningCount));

    if (!empty) {
        m_serverTable->selectRow(selectedRow >= 0 ? selectedRow : 0);
        if (!changedServerId.isEmpty() && changedServerId == selectedServerId()) {
            resetRequestLog(changedServerId);
        }
    } else {
        resetRequestLog({});
    }
}

void WebServerView::refreshRequests() {
    const QString serverId = selectedServerId();

    if (!isVisible() || serverId.isEmpty()) {
        return;
    }

    if (m_requestServerId != serverId) {
        resetRequestLog(serverId);
    }

    const auto batch = m_plugin.requestLogEntriesSince(serverId, m_requestCursor, maximumRequestBatch);
    m_requestCursor = batch.cursor;
    m_requestModel->append(batch.entries);
    m_clearRequestsButton->setEnabled(m_requestModel->rowCount() > 0);
}

void WebServerView::selectServer() {
    const QString serverId = selectedServerId();

    if (m_requestServerId != serverId) {
        resetRequestLog(serverId);
    }

    const QString name = m_plugin.webServerName(serverId);
    m_requestTitle->setText(name.isEmpty() ? m_plugin.host().translate(QStringLiteral("web-server.manager.requests")) : m_plugin.host().translate(QStringLiteral("web-server.manager.requests-for")).arg(name));
    refreshRequests();
}

// The form owns nothing the caller reads, so it is opened without taking the event loop and the click that opened it returns at once.
void WebServerView::openDialog(WebServerDialog* dialog) {
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    ui::Components::showDialogWindow(dialog, m_plugin.host().translate(QStringLiteral("web-server.dialog.title")));
}

void WebServerView::createServer() {
    openDialog(new WebServerDialog(QUuid::createUuid().toString(QUuid::WithoutBraces), {}, {}, m_plugin, this));
}

void WebServerView::createServerFromTerminal() {
    const QString terminalId = m_plugin.activeTerminalId();

    if (terminalId.isEmpty()) {
        m_plugin.host().notify(m_plugin.host().translate(QStringLiteral("web-server.plugin.title")), m_plugin.host().translate(QStringLiteral("web-server.manager.no-terminal")), plugins::AlertSeverity::Error);
        return;
    }

    QString serverId = m_plugin.serverIdForTerminal(terminalId);

    if (serverId.isEmpty()) {
        serverId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    openDialog(new WebServerDialog(serverId, terminalId, {}, m_plugin, this));
}

void WebServerView::editServerConfiguration() {
    const auto* button = qobject_cast<QToolButton*>(sender());

    if (button == nullptr) {
        return;
    }

    openServerEditor(button->property("serverId").toString());
}

// The folder arrives from another plugin, so the same form that validates every other server is the one that configures it.
void WebServerView::openServerForFolder(const QString& serverId, const QString& root) {
    openDialog(new WebServerDialog(serverId, m_plugin.webServerTerminalId(serverId), m_plugin.webServerConfigured(serverId) ? QString{} : root, m_plugin, this));
}

void WebServerView::openServerEditor(const QString& serverId) {
    if (serverId.isEmpty()) {
        return;
    }

    openDialog(new WebServerDialog(serverId, m_plugin.webServerTerminalId(serverId), {}, m_plugin, this));
}

void WebServerView::removeServerConfiguration() {
    const auto* button = qobject_cast<QToolButton*>(sender());

    if (button == nullptr) {
        return;
    }

    const QString serverId = button->property("serverId").toString();
    const bool confirmed = m_plugin.host().confirm(this, m_plugin.host().translate(QStringLiteral("web-server.dialog.remove-title")), m_plugin.host().translate(QStringLiteral("web-server.dialog.remove-message")).arg(m_plugin.webServerName(serverId)), m_plugin.host().translate(QStringLiteral("web-server.dialog.remove-detail")), m_plugin.host().translate(QStringLiteral("web-server.dialog.remove-action")), true);

    if (!confirmed) {
        return;
    }

    auto future = m_plugin.removeWebServer(serverId);
    // clang-format off
    future.then(this, [this](Result<void> result) {
        if (!result.hasValue()) {
            refreshInstances();
        }
    });
    // clang-format on
}

void WebServerView::resetRequestLog(const QString& serverId) {
    m_requestServerId = serverId;
    m_requestCursor = 0;
    m_requestModel->clear();
    m_clearRequestsButton->setEnabled(false);
}

void WebServerView::startServer() {
    const auto* button = qobject_cast<QToolButton*>(sender());

    if (button == nullptr) {
        return;
    }

    auto future = m_plugin.startWebServer(button->property("serverId").toString());
    // clang-format off
    future.then(this, [this](Result<void> result) {
        if (!result.hasValue()) {
            refreshInstances();
        }
    });
    // clang-format on
}

void WebServerView::stopServer() {
    const auto* button = qobject_cast<QToolButton*>(sender());

    if (button != nullptr) {
        m_plugin.stopWebServer(button->property("serverId").toString());
    }
}

void WebServerView::openServerInBrowser() {
    const auto* button = qobject_cast<QToolButton*>(sender());

    if (button == nullptr) {
        return;
    }

    // clang-format off
    m_plugin.openWebServerInBrowser(button->property("serverId").toString(), *this, [this](Result<QJsonObject> result) { if (!result.hasValue()) { m_plugin.host().notify(m_plugin.host().translate(QStringLiteral("web-server.plugin.title")), m_plugin.host().translate(QStringLiteral("web-server.error.browser-message")), plugins::AlertSeverity::Error); } });
    // clang-format on
}

void WebServerView::openServer() {
    const auto* button = qobject_cast<QToolButton*>(sender());

    if (button == nullptr) {
        return;
    }

    if (!m_plugin.openWebServer(button->property("serverId").toString())) {
        m_plugin.host().notify(m_plugin.host().translate(QStringLiteral("web-server.plugin.title")), m_plugin.host().translate(QStringLiteral("web-server.error.open")), plugins::AlertSeverity::Error);
    }
}

void WebServerView::clearRequests() {
    const QString serverId = selectedServerId();
    auto future = m_plugin.clearRequestLog(serverId);
    // clang-format off
    future.then(this, [this, serverId](bool cleared) {
        if (!cleared) {
            return;
        }
        resetRequestLog(serverId);
        refreshRequests();
    });
    // clang-format on
}

void WebServerView::scheduleSplitterSave(int, int) {
    const QList<int> sizes = m_splitter->sizes();
    const int total = sizes.at(0) + sizes.at(1);

    if (total <= 0) {
        return;
    }

    m_pendingSplitterRatio = std::clamp(sizes.at(0) * 1000 / total, 150, 850);
    m_splitterSaveTimer->start();
}

void WebServerView::persistSplitterRatio() {
    m_plugin.setSplitRatio(m_pendingSplitterRatio);
}

QString WebServerView::selectedServerId() const {
    const int row = m_serverTable->currentRow();
    const auto* item = row >= 0 ? m_serverTable->item(row, 0) : nullptr;
    return item == nullptr ? QString{} : item->data(Qt::UserRole).toString();
}

} // namespace workpane::plugins::webserver
