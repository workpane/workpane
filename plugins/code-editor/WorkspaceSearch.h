#pragma once

#include <QString>
#include <QVector>

namespace workpane::plugins::codeeditor {

struct WorkspaceSearchMatch final {
    QString path;
    int line{0};
    QString text;

    bool operator==(const WorkspaceSearchMatch& other) const = default;
};

struct WorkspaceSearchResult final {
    QVector<WorkspaceSearchMatch> matches;
    bool complete{true};
};

// The size of a workspace is decided by whoever opened it, so this reads it away from the interface and stops at the bounds it was given.
class WorkspaceSearches final {
  public:
    [[nodiscard]] static WorkspaceSearchResult searchWorkspace(const QString& rootPath, const QString& query, qint64 maximumFileBytes, int maximumMatches);
};

} // namespace workpane::plugins::codeeditor
