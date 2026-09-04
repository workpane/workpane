#include "TerminalWorkspaceRepository.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>
#include <limits>
#include <utility>

namespace workpane::plugins::terminalplugin {

constexpr auto workspaceScope = "workspace";

class TerminalWorkspaceRepositoryHelper final {
  public:
    static const QHash<QString, int>& layoutSlotCounts();
    static Result<domain::Workspace> failure(const QString& detail);
    static bool readString(const QJsonObject& object, const QString& key, QString& output, bool allowEmpty = false);
    static bool readInteger64(const QJsonObject& object, const QString& key, qint64& output);
    static bool readInteger(const QJsonObject& object, const QString& key, int& output);
    static bool parseLayout(const QJsonValue& value, domain::SlotLayoutState& layout);
    static bool parseSession(const QJsonValue& value, domain::TerminalSessionState& session);
    static bool parseTab(const QJsonValue& value, domain::MainTab& tab);
    static bool validateWorkspace(const domain::Workspace& workspace);
    static Result<domain::Workspace> parseWorkspace(const QJsonObject& object);
    static QJsonObject serializeLayout(const domain::SlotLayoutState& layout);
    static QJsonObject serializeWorkspace(const domain::Workspace& workspace);
};

const QHash<QString, int>& TerminalWorkspaceRepositoryHelper::layoutSlotCounts() {
    static const QHash<QString, int> counts = {{QStringLiteral("1-single"), 1}, {QStringLiteral("2-columns"), 2}, {QStringLiteral("2-rows"), 2}, {QStringLiteral("3-left"), 3}, {QStringLiteral("3-bottom"), 3}, {QStringLiteral("4-grid"), 4}, {QStringLiteral("5-balanced"), 5}, {QStringLiteral("6-columns"), 6}, {QStringLiteral("6-rows"), 6}, {QStringLiteral("7-balanced"), 7}, {QStringLiteral("8-columns"), 8}, {QStringLiteral("8-rows"), 8}, {QStringLiteral("9-grid"), 9}, {QStringLiteral("10-balanced"), 10}, {QStringLiteral("11-balanced"), 11}, {QStringLiteral("12-columns"), 12}, {QStringLiteral("12-rows"), 12}};
    return counts;
}

Result<domain::Workspace> TerminalWorkspaceRepositoryHelper::failure(const QString& detail) {
    return Result<domain::Workspace>::failure({"terminal_workspace_invalid", "The terminal workspace is invalid", detail});
}

bool TerminalWorkspaceRepositoryHelper::readString(const QJsonObject& object, const QString& key, QString& output, bool allowEmpty) {
    const QJsonValue value = object.value(key);

    if (!value.isString() || (!allowEmpty && value.toString().isEmpty())) {
        return false;
    }

    output = value.toString();
    return true;
}

bool TerminalWorkspaceRepositoryHelper::readInteger64(const QJsonObject& object, const QString& key, qint64& output) {
    return SettingsReaders::readJsonInteger(object.value(key), output);
}

bool TerminalWorkspaceRepositoryHelper::readInteger(const QJsonObject& object, const QString& key, int& output) {
    qint64 value = 0;

    if (!readInteger64(object, key, value) || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        return false;
    }

    output = static_cast<int>(value);
    return true;
}

bool TerminalWorkspaceRepositoryHelper::parseLayout(const QJsonValue& value, domain::SlotLayoutState& layout) {
    if (!value.isObject()) {
        return false;
    }

    const QJsonObject object = value.toObject();

    if (!SettingsReaders::hasExactKeys(object, {QStringLiteral("presetId"), QStringLiteral("slotCount"), QStringLiteral("slots"), QStringLiteral("shelf")}) || !readString(object, QStringLiteral("presetId"), layout.presetId) || !readInteger(object, QStringLiteral("slotCount"), layout.slotCount)) {
        return false;
    }

    const auto expectedCount = layoutSlotCounts().constFind(layout.presetId);

    if (expectedCount == layoutSlotCounts().cend() || expectedCount.value() != layout.slotCount) {
        return false;
    }

    const QJsonValue slotsValue = object.value(QStringLiteral("slots"));
    const QJsonValue shelfValue = object.value(QStringLiteral("shelf"));

    if (!slotsValue.isArray() || slotsValue.toArray().size() != layout.slotCount || !shelfValue.isArray()) {
        return false;
    }

    layout.slotAssignments.resize(layout.slotCount);
    const QJsonArray slotValues = slotsValue.toArray();

    for (int index = 0; index < slotValues.size(); ++index) {
        if (slotValues.at(index).isNull()) {
            continue;
        }
        if (!slotValues.at(index).isString() || slotValues.at(index).toString().isEmpty()) {
            return false;
        }
        layout.slotAssignments[index] = slotValues.at(index).toString();
    }

    for (const auto& entry : shelfValue.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty()) {
            return false;
        }
        layout.shelf.append(entry.toString());
    }

    return true;
}

bool TerminalWorkspaceRepositoryHelper::parseSession(const QJsonValue& value, domain::TerminalSessionState& session) {
    if (!value.isObject()) {
        return false;
    }

    const QJsonObject object = value.toObject();
    return SettingsReaders::hasExactKeys(object, {QStringLiteral("id"), QStringLiteral("workspaceId"), QStringLiteral("name"), QStringLiteral("shellProfileId"), QStringLiteral("cwd"), QStringLiteral("dynamicTitle"), QStringLiteral("createdAt"), QStringLiteral("updatedAt")}) && readString(object, QStringLiteral("id"), session.id) && readString(object, QStringLiteral("workspaceId"), session.workspaceId) && readString(object, QStringLiteral("name"), session.name) && readString(object, QStringLiteral("shellProfileId"), session.shellProfileId) && readString(object, QStringLiteral("cwd"), session.cwd) && readString(object, QStringLiteral("dynamicTitle"), session.dynamicTitle, true) && readInteger64(object, QStringLiteral("createdAt"), session.createdAt) && readInteger64(object, QStringLiteral("updatedAt"), session.updatedAt);
}

bool TerminalWorkspaceRepositoryHelper::parseTab(const QJsonValue& value, domain::MainTab& tab) {
    if (!value.isObject()) {
        return false;
    }

    const QJsonObject object = value.toObject();
    QString accentColor;

    if (!SettingsReaders::hasExactKeys(object, {QStringLiteral("id"), QStringLiteral("workspaceId"), QStringLiteral("name"), QStringLiteral("sortOrder"), QStringLiteral("accentColor"), QStringLiteral("focusedSessionId"), QStringLiteral("layout")}) || !readString(object, QStringLiteral("id"), tab.id) || !readString(object, QStringLiteral("workspaceId"), tab.workspaceId) || !readString(object, QStringLiteral("name"), tab.name) || !readInteger(object, QStringLiteral("sortOrder"), tab.sortOrder) || !readString(object, QStringLiteral("accentColor"), accentColor) || !readString(object, QStringLiteral("focusedSessionId"), tab.focusedSessionId, true) || !parseLayout(object.value(QStringLiteral("layout")), tab.layout)) {
        return false;
    }

    tab.accentColor = QColor(accentColor);
    return tab.accentColor.isValid();
}

bool TerminalWorkspaceRepositoryHelper::validateWorkspace(const domain::Workspace& workspace) {
    if (workspace.id.isEmpty() || workspace.name.trimmed().isEmpty() || workspace.name != workspace.name.trimmed() || workspace.createdAt <= 0 || workspace.updatedAt < workspace.createdAt || workspace.lastOpenedAt < workspace.createdAt || workspace.tabs.isEmpty()) {
        return false;
    }

    QSet<QString> sessionIds;

    for (const auto& session : workspace.sessions) {
        if (session.id.isEmpty() || session.workspaceId != workspace.id || sessionIds.contains(session.id) || session.name.trimmed().isEmpty() || session.name != session.name.trimmed() || session.shellProfileId.isEmpty() || !QDir::isAbsolutePath(session.cwd) || session.createdAt <= 0 || session.updatedAt < session.createdAt) {
            return false;
        }
        sessionIds.insert(session.id);
    }

    QSet<QString> tabIds;
    QSet<QString> assignedIds;

    for (int index = 0; index < workspace.tabs.size(); ++index) {
        const auto& tab = workspace.tabs.at(index);
        const auto expectedSlotCount = layoutSlotCounts().constFind(tab.layout.presetId);
        if (tab.id.isEmpty() || tab.workspaceId != workspace.id || tab.name.trimmed().isEmpty() || tab.name != tab.name.trimmed() || tab.sortOrder != index || !tab.accentColor.isValid() || tabIds.contains(tab.id) || expectedSlotCount == layoutSlotCounts().cend() || expectedSlotCount.value() != tab.layout.slotCount || tab.layout.slotAssignments.size() != tab.layout.slotCount) {
            return false;
        }
        tabIds.insert(tab.id);

        QSet<QString> visibleIds;
        for (const auto& assignment : tab.layout.slotAssignments) {
            if (!assignment.has_value()) {
                continue;
            }
            if (!sessionIds.contains(assignment.value()) || assignedIds.contains(assignment.value())) {
                return false;
            }
            assignedIds.insert(assignment.value());
            visibleIds.insert(assignment.value());
        }
        for (const auto& sessionId : tab.layout.shelf) {
            if (!sessionIds.contains(sessionId) || assignedIds.contains(sessionId)) {
                return false;
            }
            assignedIds.insert(sessionId);
        }
        if ((!visibleIds.isEmpty() && !visibleIds.contains(tab.focusedSessionId)) || (visibleIds.isEmpty() && !tab.focusedSessionId.isEmpty())) {
            return false;
        }
    }

    return tabIds.contains(workspace.selectedMainTabId) && assignedIds == sessionIds;
}

Result<domain::Workspace> TerminalWorkspaceRepositoryHelper::parseWorkspace(const QJsonObject& object) {
    if (!SettingsReaders::hasExactKeys(object, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("createdAt"), QStringLiteral("updatedAt"), QStringLiteral("lastOpenedAt"), QStringLiteral("selectedMainTabId"), QStringLiteral("tabs"), QStringLiteral("sessions")})) {
        return failure(QStringLiteral("The workspace object has unknown or missing fields"));
    }

    domain::Workspace workspace;

    if (!readString(object, QStringLiteral("id"), workspace.id) || !readString(object, QStringLiteral("name"), workspace.name) || !readInteger64(object, QStringLiteral("createdAt"), workspace.createdAt) || !readInteger64(object, QStringLiteral("updatedAt"), workspace.updatedAt) || !readInteger64(object, QStringLiteral("lastOpenedAt"), workspace.lastOpenedAt) || !readString(object, QStringLiteral("selectedMainTabId"), workspace.selectedMainTabId) || !object.value(QStringLiteral("tabs")).isArray() || !object.value(QStringLiteral("sessions")).isArray()) {
        return failure(QStringLiteral("The workspace fields are invalid"));
    }

    for (const auto& value : object.value(QStringLiteral("sessions")).toArray()) {
        domain::TerminalSessionState session;
        if (!parseSession(value, session)) {
            return failure(QStringLiteral("A terminal session is invalid"));
        }
        workspace.sessions.append(std::move(session));
    }

    for (const auto& value : object.value(QStringLiteral("tabs")).toArray()) {
        domain::MainTab tab;
        if (!parseTab(value, tab)) {
            return failure(QStringLiteral("A terminal tab is invalid"));
        }
        workspace.tabs.append(std::move(tab));
    }

    if (!validateWorkspace(workspace)) {
        return failure(QStringLiteral("Workspace references or assignments are invalid"));
    }

    return Result<domain::Workspace>::success(std::move(workspace));
}

QJsonObject TerminalWorkspaceRepositoryHelper::serializeLayout(const domain::SlotLayoutState& layout) {
    QJsonArray slotValues;

    for (const auto& assignment : layout.slotAssignments) {
        slotValues.append(assignment.has_value() ? QJsonValue(assignment.value()) : QJsonValue::Null);
    }

    QJsonArray shelf;

    for (const auto& sessionId : layout.shelf) {
        shelf.append(sessionId);
    }

    return {{QStringLiteral("presetId"), layout.presetId}, {QStringLiteral("slotCount"), layout.slotCount}, {QStringLiteral("slots"), slotValues}, {QStringLiteral("shelf"), shelf}};
}

QJsonObject TerminalWorkspaceRepositoryHelper::serializeWorkspace(const domain::Workspace& workspace) {
    QJsonArray sessions;

    for (const auto& session : workspace.sessions) {
        sessions.append(QJsonObject{{QStringLiteral("id"), session.id}, {QStringLiteral("workspaceId"), session.workspaceId}, {QStringLiteral("name"), session.name}, {QStringLiteral("shellProfileId"), session.shellProfileId}, {QStringLiteral("cwd"), session.cwd}, {QStringLiteral("dynamicTitle"), session.dynamicTitle}, {QStringLiteral("createdAt"), session.createdAt}, {QStringLiteral("updatedAt"), session.updatedAt}});
    }

    QJsonArray tabs;

    for (const auto& tab : workspace.tabs) {
        tabs.append(QJsonObject{{QStringLiteral("id"), tab.id}, {QStringLiteral("workspaceId"), tab.workspaceId}, {QStringLiteral("name"), tab.name}, {QStringLiteral("sortOrder"), tab.sortOrder}, {QStringLiteral("accentColor"), tab.accentColor.name(QColor::HexArgb)}, {QStringLiteral("focusedSessionId"), tab.focusedSessionId}, {QStringLiteral("layout"), serializeLayout(tab.layout)}});
    }

    return {{QStringLiteral("id"), workspace.id}, {QStringLiteral("name"), workspace.name}, {QStringLiteral("createdAt"), workspace.createdAt}, {QStringLiteral("updatedAt"), workspace.updatedAt}, {QStringLiteral("lastOpenedAt"), workspace.lastOpenedAt}, {QStringLiteral("selectedMainTabId"), workspace.selectedMainTabId}, {QStringLiteral("tabs"), tabs}, {QStringLiteral("sessions"), sessions}};
}

TerminalWorkspaceRepository::TerminalWorkspaceRepository(PluginHost& host) : m_host(host) {}

Result<void> TerminalWorkspaceRepository::saveInitial(const domain::Workspace& workspace) {
    if (!TerminalWorkspaceRepositoryHelper::validateWorkspace(workspace)) {
        return Result<void>::failure({"terminal_workspace_invalid", "The terminal workspace is invalid", {}});
    }

    const QString data = QString::fromUtf8(QJsonDocument(TerminalWorkspaceRepositoryHelper::serializeWorkspace(workspace)).toJson(QJsonDocument::Compact));
    return m_host.executeBootstrapDatabaseTransaction({{QStringLiteral("INSERT INTO terminal_state(scope_id, data_json) VALUES(?, ?) ON CONFLICT(scope_id) DO UPDATE SET data_json = excluded.data_json"), {QString::fromLatin1(workspaceScope), data}}});
}

QFuture<Result<void>> TerminalWorkspaceRepository::save(const domain::Workspace& workspace) {
    if (!TerminalWorkspaceRepositoryHelper::validateWorkspace(workspace)) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"terminal_workspace_invalid", "The terminal workspace is invalid", {}}));
    }

    const QString data = QString::fromUtf8(QJsonDocument(TerminalWorkspaceRepositoryHelper::serializeWorkspace(workspace)).toJson(QJsonDocument::Compact));
    return m_host.executeDatabase(QStringLiteral("INSERT INTO terminal_state(scope_id, data_json) VALUES(?, ?) ON CONFLICT(scope_id) DO UPDATE SET data_json = excluded.data_json"), {QString::fromLatin1(workspaceScope), data});
}

Result<domain::Workspace> TerminalWorkspaceRepository::loadLastOpened() const {
    const auto rows = m_host.queryBootstrapDatabase(QStringLiteral("SELECT data_json FROM terminal_state WHERE scope_id = ?"), {QString::fromLatin1(workspaceScope)});

    if (!rows.hasValue()) {
        return Result<domain::Workspace>::failure(rows.error());
    }
    if (rows.value().isEmpty()) {
        return Result<domain::Workspace>::failure({"terminal_workspace_not_found", "No saved workspace exists", {}});
    }
    if (rows.value().size() != 1) {
        return TerminalWorkspaceRepositoryHelper::failure(QStringLiteral("The workspace query returned an invalid number of rows"));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rows.value().first().value(QStringLiteral("data_json")).toString().toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return TerminalWorkspaceRepositoryHelper::failure(QStringLiteral("The workspace JSON is invalid"));
    }

    return TerminalWorkspaceRepositoryHelper::parseWorkspace(document.object());
}

} // namespace workpane::plugins::terminalplugin
