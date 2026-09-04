#pragma once

#include <QFileSystemWatcher>
#include <QString>

namespace workpane::plugins::codeeditor {

class FileWatch final {
  public:
    static void rearm(QFileSystemWatcher& watcher, const QString& path);
};

} // namespace workpane::plugins::codeeditor
