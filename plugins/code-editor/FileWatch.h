#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>

namespace workpane::plugins::codeeditor {

class FileWatch final {
  public:
    static void rearm(QFileSystemWatcher& watcher, const QString& path, QObject* owner);
};

} // namespace workpane::plugins::codeeditor
