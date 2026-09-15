#include "LogsPlugin.h"

#include "LogsTranslations.h"
#include "persistence/StoredValues.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace workpane::plugins::logs {

// The centralized log is read and cleared through capabilities of its own, because the owner of a surface is not the plugin that wants it.
constexpr auto logsPageCapability = "logs.entries.page";
constexpr auto logsClearCapability = "logs.entries.clear";
constexpr int pageSize = 100;

class LogsView final : public QWidget {
  public:
    LogsView(LogsPlugin& plugin, PluginHost& host, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_host(host) {
        setObjectName(QStringLiteral("logsView"));
        m_reloadTimer.setSingleShot(true);
        m_reloadTimer.setInterval(100);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* header = new ui::PageHeader(m_host.theme(), m_host.translate(QStringLiteral("logs.viewer.title")), this);
        m_search = new QLineEdit(header);
        m_search->setPlaceholderText(m_host.translate(QStringLiteral("logs.viewer.search")));
        m_level = new ui::ComboBox(m_host.theme(), header);
        m_level->addItem(m_host.translate(QStringLiteral("logs.viewer.all-levels")), QString{});

        for (const auto& name : {QStringLiteral("debug"), QStringLiteral("info"), QStringLiteral("warning"), QStringLiteral("error")}) {
            m_level->addItem(name.toUpper(), name);
        }

        m_loadOlder = new QPushButton(m_host.translate(QStringLiteral("logs.viewer.load-older")), header);
        auto* refresh = new QPushButton(ui::IconCatalog::icon(ui::IconName::Refresh, m_host.theme()), m_host.translate(QStringLiteral("logs.viewer.refresh")), header);
        auto* clear = new QPushButton(ui::IconCatalog::destructiveIcon(ui::IconName::Clear, m_host.theme()), m_host.translate(QStringLiteral("logs.viewer.clear")), header);
        header->addWidget(m_search, 1);
        header->addWidget(m_level);
        header->addWidget(m_loadOlder);
        header->addWidget(refresh);
        header->addWidget(clear);
        root->addWidget(header);

        m_table = ui::Components::dataGrid({m_host.translate(QStringLiteral("logs.viewer.time")), m_host.translate(QStringLiteral("logs.viewer.level")), m_host.translate(QStringLiteral("logs.viewer.source")), m_host.translate(QStringLiteral("logs.viewer.category")), m_host.translate(QStringLiteral("logs.viewer.message"))}, this);
        m_table->setObjectName(QStringLiteral("logsTable"));
        ui::Components::stretchGridColumn(m_table, 4);
        root->addWidget(m_table, 1);

        m_empty = ui::Components::emptyStateLabel(m_host.translate(QStringLiteral("logs.viewer.empty")), this);
        m_empty->hide();
        root->addWidget(m_empty, 1);

        // clang-format off
        const auto reloadEntries = [this]() {
            reload();
        };
        const auto clearEntries = [this]() {
            const bool confirmed = m_host.confirm(this, m_host.translate(QStringLiteral("logs.viewer.clear-title")), m_host.translate(QStringLiteral("logs.viewer.clear-message")), m_host.translate(QStringLiteral("logs.viewer.clear-detail")), m_host.translate(QStringLiteral("logs.viewer.clear-action")), true);
            if (!confirmed) {
                return;
            }
            auto future = m_plugin.clearEntries();
            future.then(this, [this](Result<void> result) {
                if (!result.hasValue()) {
                    m_host.notify(m_host.translate(QStringLiteral("logs.plugin.title")), m_host.translate(QStringLiteral("logs.error.clear-message")), AlertSeverity::Error);
                }
            });
        };
        const auto loadOlderEntries = [this]() {
            loadPage();
        };
        const auto renderEntries = [this]() {
            render();
        };
        const auto scheduleReload = [this]() {
            if (!m_reloadTimer.isActive()) {
                m_reloadTimer.start();
            }
        };
        // clang-format on
        connect(refresh, &QPushButton::clicked, this, reloadEntries);
        connect(clear, &QPushButton::clicked, this, clearEntries);
        connect(m_loadOlder, &QPushButton::clicked, this, loadOlderEntries);
        connect(m_search, &QLineEdit::textChanged, this, renderEntries);
        connect(m_level, &QComboBox::currentIndexChanged, this, renderEntries);
        connect(&m_reloadTimer, &QTimer::timeout, this, reloadEntries);
        connect(&m_plugin, &LogsPlugin::entriesChanged, this, scheduleReload);
        reload();
    }

  private:
    void reload() {
        ++m_reloadGeneration;
        m_entries.clear();
        m_beforeSequence = 0;
        m_pageLoading = false;
        m_loadOlder->setEnabled(false);
        loadPage();
    }

    void loadPage() {
        if (m_pageLoading) {
            return;
        }

        m_pageLoading = true;
        const quint64 generation = m_reloadGeneration;
        const qint64 requestedBeforeSequence = m_beforeSequence;
        auto future = m_plugin.entries(m_beforeSequence, pageSize);
        // clang-format off
        future.then(this, [this, generation, requestedBeforeSequence](Result<QVector<LogEntry>> result) {
            if (generation != m_reloadGeneration) {
                return;
            }
            m_pageLoading = false;
            if (requestedBeforeSequence != m_beforeSequence) {
                return;
            }
            if (!result.hasValue()) {
                m_host.notify(m_host.translate(QStringLiteral("logs.plugin.title")), m_host.translate(QStringLiteral("logs.error.read-message")), AlertSeverity::Error);
                return;
            }
            m_entries += result.value();
            if (!result.value().isEmpty()) {
                m_beforeSequence = result.value().last().sequence;
            }
            m_loadOlder->setEnabled(result.value().size() == pageSize);
            render();
        });
        // clang-format on
    }

    void render() {
        const QString search = m_search->text().trimmed();
        const QString level = m_level->currentData().toString();
        m_table->setRowCount(0);

        for (const auto& entry : m_entries) {
            if (!level.isEmpty() && entry.level != level) {
                continue;
            }
            const QString searchable = entry.sourcePluginId + QLatin1Char(' ') + entry.category + QLatin1Char(' ') + entry.message;
            if (!search.isEmpty() && !searchable.contains(search, Qt::CaseInsensitive)) {
                continue;
            }
            const int row = m_table->rowCount();
            m_table->insertRow(row);
            m_table->setItem(row, 0, new QTableWidgetItem(ui::Components::localTimestamp(entry.timestampUtc)));
            m_table->setItem(row, 1, new QTableWidgetItem(entry.level.toUpper()));
            m_table->setItem(row, 2, new QTableWidgetItem(entry.sourcePluginId));
            m_table->setItem(row, 3, new QTableWidgetItem(entry.category));
            auto* message = new QTableWidgetItem(entry.message);
            message->setToolTip(QString::fromUtf8(QJsonDocument(entry.details).toJson(QJsonDocument::Indented)));
            m_table->setItem(row, 4, message);
        }

        m_table->setVisible(m_table->rowCount() > 0);
        m_empty->setVisible(m_table->rowCount() == 0);
    }

    LogsPlugin& m_plugin;
    PluginHost& m_host;
    QLineEdit* m_search{nullptr};
    QComboBox* m_level{nullptr};
    QTableWidget* m_table{nullptr};
    QLabel* m_empty{nullptr};
    QPushButton* m_loadOlder{nullptr};
    QTimer m_reloadTimer;
    QVector<LogEntry> m_entries;
    qint64 m_beforeSequence{0};
    quint64 m_reloadGeneration{0};
    bool m_pageLoading{false};
};

class LogsPluginHelper final {
  public:
    static bool isLevel(const QString& level);
};

bool LogsPluginHelper::isLevel(const QString& level) {
    return level == QStringLiteral("debug") || level == QStringLiteral("info") || level == QStringLiteral("warning") || level == QStringLiteral("error");
}

QString LogsPlugin::id() const {
    return QStringLiteral("logs");
}

QString LogsPlugin::titleKey() const {
    return QStringLiteral("logs.plugin.title");
}

QStringList LogsPlugin::dependencies() const {
    return {};
}

int LogsPlugin::databaseSchemaVersion() const {
    return 1;
}

TranslationCatalog LogsPlugin::translations() const {
    return translations::LogsCatalog::catalog();
}

QString LogsPlugin::styleSheet(const ui::Theme&) const {
    return QStringLiteral("QTableWidget#logsTable { border: none; }");
}

QVector<NavigationItem> LogsPlugin::navigationItems(const ui::Theme& theme) const {
    return {{QStringLiteral("viewer"), QStringLiteral("logs.navigation.viewer"), ui::IconCatalog::icon(ui::IconName::Logs, theme), NavigationPlacement::Secondary, NavigationOrder::Logs}};
}

QVector<SettingsGroup> LogsPlugin::settingsGroups() const {
    const SettingsSection general{QStringLiteral("general"), QStringLiteral("logs.settings.general"), {QStringLiteral("logs.settings.storage")}};
    return {{QStringLiteral("logs"), QStringLiteral("logs.plugin.title"), {general}}};
}

Result<void> LogsPlugin::initialize(PluginHost& host) {
    if (m_host != nullptr) {
        return Result<void>::failure({"logs_already_initialized", "The Logs plugin is already initialized", {}});
    }

    m_host = &host;
    m_asyncContext = std::make_unique<QObject>();
    const auto result = m_host->migrateDatabase({{1, {QStringLiteral("CREATE TABLE logs_entries(sequence INTEGER PRIMARY KEY AUTOINCREMENT, timestamp_utc TEXT NOT NULL, source_plugin_id TEXT NOT NULL, level TEXT NOT NULL CHECK(level IN ('debug', 'info', 'warning', 'error')), category TEXT NOT NULL, message TEXT NOT NULL, details_json TEXT NOT NULL) STRICT"), QStringLiteral("CREATE INDEX logs_entries_timestamp_index ON logs_entries(timestamp_utc DESC, sequence DESC)"), QStringLiteral("CREATE INDEX logs_entries_source_index ON logs_entries(source_plugin_id, sequence DESC)")}}});

    if (!result.hasValue()) {
        shutdown();
        return result;
    }

    for (const auto* name : {logsPageCapability, logsClearCapability}) {
        const auto capability = host.provideCapability({QString::fromLatin1(name)});
        if (!capability.hasValue()) {
            shutdown();
            return capability;
        }
    }

    return result;
}

QWidget* LogsPlugin::createNavigationView(const QString& itemId, QWidget* parent) {
    return itemId == QStringLiteral("viewer") && m_host != nullptr ? new LogsView(*this, *m_host, parent) : nullptr;
}

QWidget* LogsPlugin::createSettingsSection(const QString& groupId, const QString& sectionId, QWidget* parent) {
    if (groupId != QStringLiteral("logs") || sectionId != QStringLiteral("general") || m_host == nullptr) {
        return nullptr;
    }

    const auto [view, layout] = ui::Components::settingsSectionPage(parent);
    auto* form = ui::Components::settingsForm();
    auto* clear = new QPushButton(ui::IconCatalog::destructiveIcon(ui::IconName::Clear, m_host->theme()), m_host->translate(QStringLiteral("logs.viewer.clear")), view);
    clear->setObjectName(QStringLiteral("destructiveButton"));
    clear->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("logs.settings.storage")), clear);
    layout->addLayout(form);
    // clang-format off
    const auto clearLogs = [this, view]() {
        const bool confirmed = m_host->confirm(view, m_host->translate(QStringLiteral("logs.viewer.clear-title")), m_host->translate(QStringLiteral("logs.viewer.clear-message")), m_host->translate(QStringLiteral("logs.viewer.clear-detail")), m_host->translate(QStringLiteral("logs.viewer.clear-action")), true);
        if (!confirmed) {
            return;
        }
        auto future = clearEntries();
        future.then(view, [this](Result<void> result) {
            if (!result.hasValue()) {
                m_host->notify(m_host->translate(QStringLiteral("logs.plugin.title")), m_host->translate(QStringLiteral("logs.error.clear-message")), AlertSeverity::Error);
            }
        });
    };
    // clang-format on
    connect(clear, &QPushButton::clicked, view, clearLogs);
    return view;
}

void LogsPlugin::handleRequest(const QString&, const QString& topic, const QJsonObject& payload, PluginReply reply) {
    qint64 beforeSequence = 0;
    qint64 limit = 0;

    if (topic == QString::fromLatin1(logsPageCapability) && SettingsReaders::hasExactKeys(payload, {QStringLiteral("beforeSequence"), QStringLiteral("limit")}) && SettingsReaders::readJsonInteger(payload.value(QStringLiteral("beforeSequence")), beforeSequence) && SettingsReaders::readJsonInteger(payload.value(QStringLiteral("limit")), limit) && limit <= std::numeric_limits<int>::max()) {
        auto future = entries(beforeSequence, static_cast<int>(limit));
        // clang-format off
        future.then(m_asyncContext.get(), [reply = std::move(reply)](Result<QVector<LogEntry>> result) {
            if (!result.hasValue()) {
                reply(Result<QJsonObject>::failure(result.error()));
                return;
            }
            QJsonArray values;
            for (const auto& entry : result.value()) {
                values.append(QJsonObject{{QStringLiteral("sequence"), entry.sequence}, {QStringLiteral("timestampUtc"), persistence::StoredValues::storedTimestamp(entry.timestampUtc)}, {QStringLiteral("sourcePluginId"), entry.sourcePluginId}, {QStringLiteral("level"), entry.level}, {QStringLiteral("category"), entry.category}, {QStringLiteral("message"), entry.message}, {QStringLiteral("details"), entry.details}});
            }
            reply(Result<QJsonObject>::success({{QStringLiteral("entries"), values}}));
        });
        // clang-format on
        return;
    }

    if (topic == QString::fromLatin1(logsClearCapability) && payload.isEmpty()) {
        auto future = clearEntries();
        // clang-format off
        future.then(m_asyncContext.get(), [reply = std::move(reply)](Result<void> result) { reply(result.hasValue() ? Result<QJsonObject>::success({}) : Result<QJsonObject>::failure(result.error())); });
        // clang-format on
        return;
    }

    reply(SettingsReaders::unhandledTopic(topic));
}

void LogsPlugin::handleEvent(const QString& senderPluginId, const QString& topic, const QJsonObject& payload) {
    if (topic != QStringLiteral("workpane.log.entry")) {
        return;
    }

    auto future = appendEntry(senderPluginId, payload);
    Q_UNUSED(future);
}

void LogsPlugin::shutdown() {
    m_asyncContext.reset();
    m_host = nullptr;
}

QFuture<Result<QVector<LogEntry>>> LogsPlugin::entries(qint64 beforeSequence, int limit) {
    if (m_host == nullptr || beforeSequence < 0 || limit < 1 || limit > 100) {
        return QtFuture::makeReadyValueFuture(Result<QVector<LogEntry>>::failure({"logs_page_invalid", "The requested log page is invalid", {}}));
    }

    const QString statement = beforeSequence == 0 ? QStringLiteral("SELECT sequence, timestamp_utc, source_plugin_id, level, category, message, details_json FROM logs_entries ORDER BY sequence DESC LIMIT ?") : QStringLiteral("SELECT sequence, timestamp_utc, source_plugin_id, level, category, message, details_json FROM logs_entries WHERE sequence < ? ORDER BY sequence DESC LIMIT ?");
    const QVariantList bindings = beforeSequence == 0 ? QVariantList{limit} : QVariantList{beforeSequence, limit};
    auto future = m_host->queryDatabase(statement, bindings);
    // clang-format off
    return future.then([](Result<persistence::DatabaseRows> rows) {
        if (!rows.hasValue()) {
            return Result<QVector<LogEntry>>::failure(rows.error());
        }
        QVector<LogEntry> values;
        values.reserve(rows.value().size());
        for (const auto& row : rows.value()) {
            qint64 sequence = 0;
            const QDateTime timestampUtc = persistence::StoredValues::parseStoredTimestamp(row.value(QStringLiteral("timestamp_utc")));
            const QString sourcePluginId = row.value(QStringLiteral("source_plugin_id")).toString();
            const QString level = row.value(QStringLiteral("level")).toString();
            const QString category = row.value(QStringLiteral("category")).toString();
            const QString message = row.value(QStringLiteral("message")).toString();
            QJsonParseError error;
            const QJsonDocument details = QJsonDocument::fromJson(row.value(QStringLiteral("details_json")).toString().toUtf8(), &error);
            if (!persistence::StoredValues::readStoredInteger(row.value(QStringLiteral("sequence")), sequence) || sequence < 1 || !persistence::StoredValues::validStoredTimestamp(timestampUtc) || sourcePluginId.isEmpty() || !LogsPluginHelper::isLevel(level) || category.trimmed().isEmpty() || message.trimmed().isEmpty() || error.error != QJsonParseError::NoError || !details.isObject()) {
                return Result<QVector<LogEntry>>::failure({"logs_entry_invalid", "A stored log entry is invalid", error.errorString()});
            }
            values.append({sequence, timestampUtc, sourcePluginId, level, category, message, details.object()});
        }
        return Result<QVector<LogEntry>>::success(std::move(values));
    });
    // clang-format on
}

QFuture<Result<void>> LogsPlugin::clearEntries() {
    if (m_host == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"logs_unavailable", "The Logs plugin is unavailable", {}}));
    }

    auto future = m_host->executeDatabase(QStringLiteral("DELETE FROM logs_entries"));
    // clang-format off
    return future.then(m_asyncContext.get(), [this](Result<void> result) {
        if (result.hasValue()) {
            emit entriesChanged();
        }
        return result;
    });
    // clang-format on
}

QFuture<Result<void>> LogsPlugin::appendEntry(const QString& senderPluginId, const QJsonObject& payload) {
    if (m_host == nullptr || senderPluginId.isEmpty() || !SettingsReaders::hasExactKeys(payload, {QStringLiteral("timestampUtc"), QStringLiteral("level"), QStringLiteral("category"), QStringLiteral("message"), QStringLiteral("details")}) || !payload.value(QStringLiteral("timestampUtc")).isString() || !payload.value(QStringLiteral("level")).isString() || !payload.value(QStringLiteral("category")).isString() || !payload.value(QStringLiteral("message")).isString() || !payload.value(QStringLiteral("details")).isObject()) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"logs_entry_invalid", "The log entry is invalid", senderPluginId}));
    }

    const QString timestamp = payload.value(QStringLiteral("timestampUtc")).toString();
    const QDateTime timestampUtc = persistence::StoredValues::parseStoredTimestamp(timestamp);
    const QString level = payload.value(QStringLiteral("level")).toString();
    const QString category = payload.value(QStringLiteral("category")).toString();
    const QString message = payload.value(QStringLiteral("message")).toString();

    if (!persistence::StoredValues::validStoredTimestamp(timestampUtc) || !LogsPluginHelper::isLevel(level) || category.trimmed().isEmpty() || message.trimmed().isEmpty()) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"logs_entry_invalid", "The log entry is invalid", senderPluginId}));
    }

    const QString details = QString::fromUtf8(QJsonDocument(payload.value(QStringLiteral("details")).toObject()).toJson(QJsonDocument::Compact));
    auto future = m_host->executeDatabase(QStringLiteral("INSERT INTO logs_entries(timestamp_utc, source_plugin_id, level, category, message, details_json) VALUES(?, ?, ?, ?, ?, ?)"), {timestamp, senderPluginId, level, category, message, details});
    // clang-format off
    return future.then(m_asyncContext.get(), [this](Result<void> result) {
        if (result.hasValue()) {
            emit entriesChanged();
        }
        return result;
    });
    // clang-format on
}

} // namespace workpane::plugins::logs
