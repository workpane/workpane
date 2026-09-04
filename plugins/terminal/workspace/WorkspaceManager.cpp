#include "workspace/WorkspaceManager.h"

#include "TerminalFailure.h"
#include "terminal/ShellProfile.h"
#include "terminal/TerminalThemeCatalog.h"
#include "ui/Theme.h"
#include "workspace/LayoutManager.h"

#include <QDateTime>
#include <QDir>
#include <QSignalBlocker>
#include <QUuid>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace workpane::plugins::terminalplugin::workspace {

class WorkspaceManagerHelper final {
  public:
    static QString newId();
    static std::int64_t now();
    static QVariantList optionalSessionIds(const QVector<std::optional<QString>>& assignments);
};

QString WorkspaceManagerHelper::newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

std::int64_t WorkspaceManagerHelper::now() {
    return QDateTime::currentMSecsSinceEpoch();
}

QVariantList WorkspaceManagerHelper::optionalSessionIds(const QVector<std::optional<QString>>& assignments) {
    QVariantList result;
    result.reserve(assignments.size());

    for (const auto& slot : assignments) {
        result.append(slot.has_value() ? QVariant(slot.value()) : QVariant(QString{}));
    }

    return result;
}

WorkspaceManager::WorkspaceManager(plugins::terminalplugin::TerminalWorkspaceRepository& repository, plugins::PluginHost& host, QString historyPath, domain::TerminalTheme terminalTheme, terminalcore::PtyBackendFactory backendFactory, QObject* parent) : QAbstractListModel(parent), m_repository(repository), m_host(host), m_historyPath(std::move(historyPath)), m_terminalTheme(std::move(terminalTheme)), m_backendFactory(std::move(backendFactory)) {}

WorkspaceManager::~WorkspaceManager() {
    shutdown();
}

int WorkspaceManager::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_workspace.tabs.size());
}

QVariant WorkspaceManager::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_workspace.tabs.size() || role != NameRole) {
        return {};
    }

    return m_workspace.tabs.at(index.row()).name;
}

int WorkspaceManager::currentTabIndex() const {
    return m_currentTabIndex;
}

void WorkspaceManager::setCurrentTabIndex(int index) {
    if (index < 0 || index >= m_workspace.tabs.size()) {
        return;
    }
    if (index == m_currentTabIndex && m_workspace.selectedMainTabId == m_workspace.tabs.at(index).id) {
        return;
    }

    m_currentTabIndex = index;
    m_workspace.selectedMainTabId = m_workspace.tabs.at(index).id;
    m_workspace.lastOpenedAt = WorkspaceManagerHelper::now();
    emit currentTabChanged();
    persist();
}

QString WorkspaceManager::currentTabId() const {
    const auto* tab = currentTab();
    return tab == nullptr ? QString{} : tab->id;
}

QVariantList WorkspaceManager::currentSlots() const {
    const auto* tab = currentTab();
    return tab == nullptr ? QVariantList{} : WorkspaceManagerHelper::optionalSessionIds(tab->layout.slotAssignments);
}

QVariantList WorkspaceManager::currentShelf() const {
    const auto* tab = currentTab();

    if (tab == nullptr) {
        return {};
    }

    QVariantList result;
    result.reserve(tab->layout.shelf.size());

    for (const auto& sessionId : tab->layout.shelf) {
        result.append(sessionId);
    }

    return result;
}

QString WorkspaceManager::currentPresetId() const {
    const auto* tab = currentTab();
    return tab == nullptr ? QString{} : tab->layout.presetId;
}

QString WorkspaceManager::currentFocusedSessionId() const {
    const auto* tab = currentTab();
    return tab == nullptr ? QString{} : tab->focusedSessionId;
}

QString WorkspaceManager::sessionName(const QString& sessionId) const {
    const auto session = std::ranges::find(m_workspace.sessions, sessionId, &domain::TerminalSessionState::id);
    return session == m_workspace.sessions.end() ? QString{} : session->name;
}

int WorkspaceManager::currentLayoutColumns() const {
    const auto* tab = currentTab();
    const auto preset = tab == nullptr ? Result<domain::LayoutPreset>::failure({}) : LayoutManager::preset(tab->layout.presetId);
    return preset.hasValue() ? preset.value().columns : 1;
}

QString WorkspaceManager::currentCwd() const {
    const auto* tab = currentTab();

    if (tab == nullptr) {
        return {};
    }

    const auto* session = runtimeSession(tab->focusedSessionId);
    return session == nullptr ? QString{} : session->cwd();
}

int WorkspaceManager::terminalCount() const {
    return static_cast<int>(m_workspace.sessions.size());
}

QVariantList WorkspaceManager::allSessions() const {
    QVariantList sessions;
    sessions.reserve(m_workspace.sessions.size());

    for (const auto& session : m_workspace.sessions) {
        sessions.append(sessionData(session.id));
    }

    return sessions;
}

Result<void> WorkspaceManager::initialize() {
    const auto loaded = m_repository.loadLastOpened();

    if (loaded.hasValue()) {
        m_workspace = loaded.value();
    } else if (loaded.error().code == QStringLiteral("terminal_workspace_not_found")) {
        m_workspace = createDefaultWorkspace();
        const auto saved = m_repository.saveInitial(m_workspace);
        if (!saved.hasValue()) {
            return saved;
        }
    } else {
        return Result<void>::failure(loaded.error());
    }

    m_currentTabIndex = 0;

    for (int index = 0; index < m_workspace.tabs.size(); ++index) {
        if (m_workspace.tabs.at(index).id == m_workspace.selectedMainTabId) {
            m_currentTabIndex = index;
            break;
        }
    }

    beginResetModel();
    const auto sessionsResult = startRuntimeSessions();

    if (!sessionsResult.hasValue()) {
        endResetModel();
        return sessionsResult;
    }

    endResetModel();
    emit currentTabChanged();
    emit sessionsChanged();
    return Result<void>::success();
}

void WorkspaceManager::shutdown(ShutdownMode mode) {
    if (m_shuttingDown) {
        return;
    }

    m_shuttingDown = true;

    if (mode == ShutdownMode::Persist) {
        persist();
    }

    for (auto& [id, session] : m_runtimeSessions) {
        Q_UNUSED(id);
        session->terminate();
    }

    m_runtimeSessions.clear();
}

void WorkspaceManager::setTerminalTheme(const QString& themeId) {
    if (!terminalcore::TerminalThemes::terminalThemeExists(themeId)) {
        return;
    }

    const auto* selected = terminalcore::TerminalThemes::terminalTheme(themeId);

    if (selected == nullptr) {
        return;
    }

    m_terminalTheme = *selected;

    for (auto& [id, session] : m_runtimeSessions) {
        Q_UNUSED(id);
        session->setTheme(m_terminalTheme);
    }
}

void WorkspaceManager::setClipboardWriteAllowed(bool allowed) {
    m_clipboardWriteAllowed = allowed;

    for (auto& [id, session] : m_runtimeSessions) {
        Q_UNUSED(id);
        session->setClipboardWriteAllowed(allowed);
    }
}

QObject* WorkspaceManager::sessionObject(const QString& sessionId) const {
    return runtimeSession(sessionId);
}

QVariantMap WorkspaceManager::sessionData(const QString& sessionId) const {
    const auto* session = runtimeSession(sessionId);

    if (session == nullptr) {
        return {};
    }

    return {{QStringLiteral("id"), session->id()}, {QStringLiteral("name"), session->name()}, {QStringLiteral("cwd"), session->cwd()}, {QStringLiteral("shell"), session->shellName()}, {QStringLiteral("status"), session->status()}, {QStringLiteral("exitCode"), session->exitCode()}};
}

QVariantList WorkspaceManager::layoutPresets() const {
    QVariantList output;

    for (const auto& preset : LayoutManager::presets()) {
        output.append(QVariantMap{{QStringLiteral("id"), preset.id}, {QStringLiteral("name"), preset.name}, {QStringLiteral("slotCount"), preset.slotCount}, {QStringLiteral("columns"), preset.columns}, {QStringLiteral("rows"), preset.rows}});
    }

    return output;
}

void WorkspaceManager::createTab() {
    const int index = static_cast<int>(m_workspace.tabs.size());
    beginInsertRows({}, index, index);

    domain::MainTab tab;
    tab.id = WorkspaceManagerHelper::newId();
    tab.workspaceId = m_workspace.id;
    tab.name = m_host.translate(QStringLiteral("terminal.workspace.numbered")).arg(index + 1);
    tab.sortOrder = index;
    tab.accentColor = m_host.theme().color(ui::ThemeColor::Accent);
    m_workspace.tabs.append(std::move(tab));

    endInsertRows();
    setCurrentTabIndex(index);
}

void WorkspaceManager::moveTab(int from, int to) {
    if (from < 0 || from >= m_workspace.tabs.size() || to < 0 || to >= m_workspace.tabs.size() || from == to) {
        return;
    }

    const int destination = from < to ? to + 1 : to;

    if (!beginMoveRows({}, from, from, {}, destination)) {
        return;
    }

    m_workspace.tabs.move(from, to);

    for (int index = 0; index < m_workspace.tabs.size(); ++index) {
        m_workspace.tabs[index].sortOrder = index;
        if (m_workspace.tabs.at(index).id == m_workspace.selectedMainTabId) {
            m_currentTabIndex = index;
        }
    }

    endMoveRows();

    m_workspace.lastOpenedAt = WorkspaceManagerHelper::now();
    emit currentTabChanged();
    persist();
}

void WorkspaceManager::renameTab(int index, const QString& name) {
    if (index < 0 || index >= m_workspace.tabs.size() || name.trimmed().isEmpty()) {
        return;
    }

    m_workspace.tabs[index].name = name.trimmed();
    m_workspace.updatedAt = WorkspaceManagerHelper::now();
    emit dataChanged(this->index(index), this->index(index), {NameRole});
    persist();
}

void WorkspaceManager::closeTab(int index) {
    if (index < 0 || index >= m_workspace.tabs.size()) {
        return;
    }

    const auto tab = m_workspace.tabs.at(index);
    QVector<QString> sessionIds;

    for (const auto& slot : tab.layout.slotAssignments) {
        if (slot.has_value()) {
            sessionIds.append(slot.value());
        }
    }

    sessionIds.append(tab.layout.shelf);

    for (const auto& sessionId : sessionIds) {
        emit terminalClosing(sessionId);
    }

    const QString selectedTabId = m_workspace.selectedMainTabId;
    const bool closingSelectedTab = tab.id == selectedTabId;
    beginRemoveRows({}, index, index);
    m_workspace.tabs.removeAt(index);

    for (int tabIndex = 0; tabIndex < m_workspace.tabs.size(); ++tabIndex) {
        m_workspace.tabs[tabIndex].sortOrder = tabIndex;
    }

    if (!m_workspace.tabs.isEmpty() && !closingSelectedTab) {
        const auto selected = std::ranges::find(m_workspace.tabs, selectedTabId, &domain::MainTab::id);
        m_currentTabIndex = selected == m_workspace.tabs.end() ? std::min(index, static_cast<int>(m_workspace.tabs.size()) - 1) : static_cast<int>(std::distance(m_workspace.tabs.begin(), selected));
    }

    if (!m_workspace.tabs.isEmpty() && closingSelectedTab) {
        m_currentTabIndex = std::min(index, static_cast<int>(m_workspace.tabs.size()) - 1);
    }

    if (!m_workspace.tabs.isEmpty()) {
        m_workspace.selectedMainTabId = m_workspace.tabs.at(m_currentTabIndex).id;
    }

    if (m_workspace.tabs.isEmpty()) {
        m_currentTabIndex = -1;
        m_workspace.selectedMainTabId.clear();
    }

    endRemoveRows();

    for (const auto& sessionId : sessionIds) {
        const auto state = std::ranges::find(m_workspace.sessions, sessionId, &domain::TerminalSessionState::id);
        if (state != m_workspace.sessions.end()) {
            m_workspace.sessions.erase(state);
        }
    }

    const bool needsDefaultTab = m_workspace.tabs.isEmpty();
    emit sessionsChanged();

    if (needsDefaultTab) {
        createTab();
    }

    if (!needsDefaultTab) {
        emit currentTabChanged();
    }

    for (const auto& sessionId : sessionIds) {
        auto closing = m_runtimeSessions.extract(sessionId);
        if (closing.empty()) {
            continue;
        }

        const QSignalBlocker blocker(closing.mapped().get());
        closing.mapped()->terminate();
    }

    if (needsDefaultTab) {
        createTerminal(0);
        return;
    }

    persist();
}

QString WorkspaceManager::createTerminal(int slotIndex) {
    auto* tab = currentTab();

    if (tab == nullptr || m_backendFactory == nullptr) {
        return {};
    }

    auto backend = m_backendFactory();

    if (backend == nullptr) {
        return {};
    }

    auto state = createSessionState();
    const QString sessionId = state.id;
    const auto profile = terminalcore::ShellProfileResolver::systemDefault();
    auto session = std::make_unique<terminalcore::TerminalSession>(state, profile, m_terminalTheme, std::move(backend));
    session->setClipboardWriteAllowed(m_clipboardWriteAllowed);
    connect(session.get(), &terminalcore::TerminalSession::stateChanged, this, &WorkspaceManager::persistRuntimeSession);
    connect(session.get(), &terminalcore::TerminalSession::nameChanged, this, &WorkspaceManager::notifySessionNameChanged);
    connect(session.get(), &terminalcore::TerminalSession::errorOccurred, this, &WorkspaceManager::reportSessionError);

    m_workspace.sessions.append(state);
    m_runtimeSessions.emplace(sessionId, std::move(session));

    int destinationSlot = -1;

    if (slotIndex >= 0 && slotIndex < tab->layout.slotCount && !tab->layout.slotAssignments.at(slotIndex).has_value()) {
        destinationSlot = slotIndex;
    }

    if (destinationSlot < 0) {
        // clang-format off
        const auto emptySlot = std::ranges::find_if(tab->layout.slotAssignments, [](const auto& assignment) { return !assignment.has_value(); });
        // clang-format on
        if (emptySlot != tab->layout.slotAssignments.end()) {
            destinationSlot = static_cast<int>(std::distance(tab->layout.slotAssignments.begin(), emptySlot));
        }
    }

    if (destinationSlot < 0) {
        destinationSlot = LayoutManager::visibleSlotIndex(tab->layout, tab->focusedSessionId);
    }

    if (!LayoutManager::assignToSlot(tab->layout, sessionId, destinationSlot).hasValue()) {
        LayoutManager::moveToShelf(tab->layout, sessionId);
    }

    tab->focusedSessionId = sessionId;

    const auto result = runtimeSession(sessionId)->start();

    if (!result.hasValue()) {
        emit notificationRequested(m_host.translate(QStringLiteral("terminal.error.start-title")), plugins::terminalplugin::TerminalFailures::terminalFailureMessage(result.error(), m_host), true);
    }

    notifyTabChanged(m_currentTabIndex);
    emit sessionsChanged();
    persist();
    return sessionId;
}

// The runtime leaves the collection before anything is announced, so no signal can reach a session this is already closing.
void WorkspaceManager::closeTerminal(QString sessionId) {
    auto closing = m_runtimeSessions.extract(sessionId);

    if (closing.empty()) {
        return;
    }

    const int owningTabIndex = tabIndexForSession(sessionId);

    emit terminalClosing(sessionId);
    removeSessionFromAllTabs(sessionId);

    const auto state = std::ranges::find(m_workspace.sessions, sessionId, &domain::TerminalSessionState::id);

    if (state != m_workspace.sessions.end()) {
        m_workspace.sessions.erase(state);
    }

    notifyTabChanged(owningTabIndex);
    emit sessionsChanged();

    const QSignalBlocker blocker(closing.mapped().get());
    closing.mapped()->terminate();
    persist();
}

void WorkspaceManager::changeLayout(const QString& presetId) {
    auto* tab = currentTab();

    if (tab == nullptr) {
        return;
    }
    if (tab->layout.presetId == presetId) {
        return;
    }

    if (!LayoutManager::changePreset(tab->layout, presetId).hasValue()) {
        emit notificationRequested(m_host.translate(QStringLiteral("terminal.error.layout-title")), m_host.translate(QStringLiteral("terminal.error.layout-message")), true);
        return;
    }

    normalizeFocusedSession(*tab);
    notifyTabChanged(m_currentTabIndex);
    persist();
}

void WorkspaceManager::assignToSlot(const QString& sessionId, int slotIndex) {
    auto* tab = currentTab();

    if (tab == nullptr || !LayoutManager::contains(tab->layout, sessionId)) {
        return;
    }

    if (!LayoutManager::assignToSlot(tab->layout, sessionId, slotIndex).hasValue()) {
        emit notificationRequested(m_host.translate(QStringLiteral("terminal.error.slot-title")), m_host.translate(QStringLiteral("terminal.error.slot-message")), true);
        return;
    }

    tab->focusedSessionId = sessionId;
    notifyTabChanged(m_currentTabIndex);
    persist();
}

void WorkspaceManager::activateShelvedSession(const QString& sessionId) {
    auto* tab = currentTab();

    if (tab == nullptr || !tab->layout.shelf.contains(sessionId)) {
        return;
    }

    int destinationSlot = -1;
    // clang-format off
    const auto emptySlot = std::ranges::find_if(tab->layout.slotAssignments, [](const auto& assignment) { return !assignment.has_value(); });
    // clang-format on

    if (emptySlot != tab->layout.slotAssignments.end()) {
        destinationSlot = static_cast<int>(std::distance(tab->layout.slotAssignments.begin(), emptySlot));
    }

    if (destinationSlot < 0) {
        destinationSlot = LayoutManager::visibleSlotIndex(tab->layout, tab->focusedSessionId);
    }

    if (!LayoutManager::assignToSlot(tab->layout, sessionId, destinationSlot).hasValue()) {
        LayoutManager::moveToShelf(tab->layout, sessionId);
    }

    tab->focusedSessionId = sessionId;
    notifyTabChanged(m_currentTabIndex);
    persist();
}

void WorkspaceManager::moveToShelf(const QString& sessionId, int shelfIndex) {
    auto* tab = currentTab();

    if (tab == nullptr || !LayoutManager::contains(tab->layout, sessionId)) {
        return;
    }

    LayoutManager::moveToShelf(tab->layout, sessionId, shelfIndex);
    normalizeFocusedSession(*tab);
    notifyTabChanged(m_currentTabIndex);
    persist();
}

void WorkspaceManager::focusSession(const QString& sessionId) {
    auto* tab = currentTab();

    if (tab == nullptr || LayoutManager::visibleSlotIndex(tab->layout, sessionId) < 0) {
        return;
    }

    tab->focusedSessionId = sessionId;
    emit focusedSessionChanged(sessionId);
    emit currentTabChanged();
    persist();
}

void WorkspaceManager::persistRuntimeSession() {
    auto* runtime = qobject_cast<terminalcore::TerminalSession*>(sender());

    if (runtime == nullptr) {
        return;
    }

    auto* state = sessionState(runtime->id());

    if (state != nullptr) {
        *state = runtime->state();
    }

    const auto* tab = currentTab();

    if (tab != nullptr && tab->focusedSessionId == runtime->id()) {
        emit currentTabChanged();
    }

    persist();
}

void WorkspaceManager::notifySessionNameChanged() {
    const auto* runtime = qobject_cast<terminalcore::TerminalSession*>(sender());

    if (runtime != nullptr) {
        emit sessionNameChanged(runtime->id());
    }
}

void WorkspaceManager::reportSessionError(const Error& error) {
    emit notificationRequested(m_host.translate(QStringLiteral("terminal.error.runtime-title")), plugins::terminalplugin::TerminalFailures::terminalFailureMessage(error, m_host), true);
}

domain::MainTab* WorkspaceManager::currentTab() {
    if (m_currentTabIndex < 0 || m_currentTabIndex >= m_workspace.tabs.size()) {
        return nullptr;
    }

    return &m_workspace.tabs[m_currentTabIndex];
}

const domain::MainTab* WorkspaceManager::currentTab() const {
    if (m_currentTabIndex < 0 || m_currentTabIndex >= m_workspace.tabs.size()) {
        return nullptr;
    }

    return &m_workspace.tabs.at(m_currentTabIndex);
}

domain::TerminalSessionState* WorkspaceManager::sessionState(const QString& sessionId) {
    const auto match = std::ranges::find(m_workspace.sessions, sessionId, &domain::TerminalSessionState::id);
    return match == m_workspace.sessions.end() ? nullptr : &*match;
}

terminalcore::TerminalSession* WorkspaceManager::runtimeSession(const QString& sessionId) const {
    const auto match = m_runtimeSessions.find(sessionId);
    return match == m_runtimeSessions.end() ? nullptr : match->second.get();
}

domain::Workspace WorkspaceManager::createDefaultWorkspace() const {
    domain::Workspace workspace;
    workspace.id = WorkspaceManagerHelper::newId();
    workspace.name = m_host.translate(QStringLiteral("terminal.workspace.default-name"));
    workspace.createdAt = WorkspaceManagerHelper::now();
    workspace.updatedAt = workspace.createdAt;
    workspace.lastOpenedAt = workspace.createdAt;

    domain::MainTab tab;
    tab.id = WorkspaceManagerHelper::newId();
    tab.workspaceId = workspace.id;
    tab.name = m_host.translate(QStringLiteral("terminal.workspace.numbered")).arg(1);
    tab.accentColor = m_host.theme().color(ui::ThemeColor::Accent);

    auto session = createSessionState();
    session.workspaceId = workspace.id;
    tab.layout.slotAssignments[0] = session.id;
    tab.focusedSessionId = session.id;

    workspace.selectedMainTabId = tab.id;
    workspace.tabs.append(std::move(tab));
    workspace.sessions.append(std::move(session));
    return workspace;
}

domain::TerminalSessionState WorkspaceManager::createSessionState() const {
    const auto profile = terminalcore::ShellProfileResolver::systemDefault();
    domain::TerminalSessionState state;
    state.id = WorkspaceManagerHelper::newId();
    state.workspaceId = m_workspace.id;
    state.name = profile.name;
    state.shellProfileId = profile.id;
    state.cwd = QDir::homePath();
    state.historyFile = QDir(m_historyPath).filePath(state.id + QStringLiteral(".history"));
    state.createdAt = WorkspaceManagerHelper::now();
    state.updatedAt = state.createdAt;
    return state;
}

Result<void> WorkspaceManager::startRuntimeSessions() {
    if (m_backendFactory == nullptr) {
        return Result<void>::failure({"terminal_backend_unavailable", "The terminal backend factory is unavailable", {}});
    }

    const auto profiles = terminalcore::ShellProfileResolver::availableProfiles();

    for (auto& state : m_workspace.sessions) {
        const auto savedProfile = std::ranges::find(profiles, state.shellProfileId, &terminalcore::ShellProfile::id);
        if (savedProfile == profiles.end()) {
            return Result<void>::failure({"shell_profile_unavailable", "A saved shell profile is unavailable", state.shellProfileId});
        }
        auto backend = m_backendFactory();
        if (backend == nullptr) {
            return Result<void>::failure({"terminal_backend_unavailable", "The terminal backend factory is unavailable", state.id});
        }

        state.historyFile = QDir(m_historyPath).filePath(state.id + QStringLiteral(".history"));
        auto session = std::make_unique<terminalcore::TerminalSession>(state, *savedProfile, m_terminalTheme, std::move(backend));
        session->setClipboardWriteAllowed(m_clipboardWriteAllowed);
        connect(session.get(), &terminalcore::TerminalSession::stateChanged, this, &WorkspaceManager::persistRuntimeSession);
        connect(session.get(), &terminalcore::TerminalSession::nameChanged, this, &WorkspaceManager::notifySessionNameChanged);
        connect(session.get(), &terminalcore::TerminalSession::errorOccurred, this, &WorkspaceManager::reportSessionError);
        const QString id = state.id;
        m_runtimeSessions.emplace(id, std::move(session));
        const auto result = runtimeSession(id)->start();
        if (!result.hasValue()) {
            emit notificationRequested(m_host.translate(QStringLiteral("terminal.error.start-title")), plugins::terminalplugin::TerminalFailures::terminalFailureMessage(result.error(), m_host), true);
        }
    }

    return Result<void>::success();
}

void WorkspaceManager::removeSessionFromAllTabs(const QString& sessionId) {
    for (auto& tab : m_workspace.tabs) {
        LayoutManager::remove(tab.layout, sessionId);
        if (tab.focusedSessionId != sessionId) {
            continue;
        }

        normalizeFocusedSession(tab);
    }
}

int WorkspaceManager::tabIndexForSession(const QString& sessionId) const {
    for (int index = 0; index < m_workspace.tabs.size(); ++index) {
        if (LayoutManager::contains(m_workspace.tabs.at(index).layout, sessionId)) {
            return index;
        }
    }

    return -1;
}

void WorkspaceManager::normalizeFocusedSession(domain::MainTab& tab) {
    if (LayoutManager::visibleSlotIndex(tab.layout, tab.focusedSessionId) >= 0) {
        return;
    }

    tab.focusedSessionId.clear();

    for (const auto& assignment : tab.layout.slotAssignments) {
        if (assignment.has_value()) {
            tab.focusedSessionId = assignment.value();
            return;
        }
    }
}

void WorkspaceManager::persist() {
    if (m_workspace.id.isEmpty()) {
        return;
    }

    m_workspace.updatedAt = WorkspaceManagerHelper::now();
    auto future = m_repository.save(m_workspace);
    // clang-format off
    future.then(this, [this](Result<void> result) {
        if (!result.hasValue() && !m_shuttingDown) {
            emit notificationRequested(m_host.translate(QStringLiteral("terminal.error.save-title")), plugins::terminalplugin::TerminalFailures::terminalFailureMessage(result.error(), m_host), true);
        }
    });
    // clang-format on
}

void WorkspaceManager::notifyTabChanged(int index) {
    if (index < 0 || index >= m_workspace.tabs.size()) {
        return;
    }

    if (index == m_currentTabIndex) {
        emit layoutChanged();
        emit currentTabChanged();
    }
}

} // namespace workpane::plugins::terminalplugin::workspace
