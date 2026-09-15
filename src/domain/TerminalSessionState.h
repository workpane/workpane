#pragma once

#include <QString>
#include <QtTypes>

namespace workpane::domain {

enum class TerminalProcessState { Starting, Running, Exited, Failed };
struct TerminalSessionState final {
    QString id;
    QString workspaceId;
    QString name;
    QString shellProfileId;
    QString cwd;
    QString historyFile;
    QString dynamicTitle;
    qint64 createdAt{};
    qint64 updatedAt{};
    TerminalProcessState processState{TerminalProcessState::Starting};
};

} // namespace workpane::domain
