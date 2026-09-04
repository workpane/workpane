#include "FileWatch.h"

#include <QFileInfo>

namespace workpane::plugins::codeeditor {

// A watched path is reported only once on macOS, so every notification rearms the watch it came from and a second external change is still seen.
void FileWatch::rearm(QFileSystemWatcher& watcher, const QString& path) {
    watcher.removePath(path);

    if (QFileInfo::exists(path)) {
        watcher.addPath(path);
    }
}

} // namespace workpane::plugins::codeeditor
