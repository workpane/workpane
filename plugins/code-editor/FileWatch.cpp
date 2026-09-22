#include "FileWatch.h"

#include <QFileInfo>
#include <QTimer>

namespace workpane::plugins::codeeditor {

// A file replaced rather than written into is dropped by the watcher itself, which is why a second external edit is never seen until the path is watched again.
// Only what the watcher no longer holds is added back, because removing what it still holds restarts the stream of the platform a second time for nothing.
// That happens once the event loop comes back to it, because the engine is still delivering the notification that asked for it.
void FileWatch::rearm(QFileSystemWatcher& watcher, const QString& path, QObject* owner) {
    // clang-format off
    QTimer::singleShot(0, owner, [&watcher, path]() { if (!watcher.files().contains(path) && !watcher.directories().contains(path) && QFileInfo::exists(path)) { watcher.addPath(path); } });
    // clang-format on
}

} // namespace workpane::plugins::codeeditor
