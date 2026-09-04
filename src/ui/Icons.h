#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QVector>

namespace workpane::ui {

class Theme;

enum class IconName { Add, Terminal, Layout, Search, Settings, Close, More, Focus, Restore, WebServer, Browser, Start, Stop, Back, Forward, Home, Shelf, Bookmark, Folder, Edit, Refresh, Clear, Import, Export, ExternalLink, Logs, Tasks, Workspace, Donate, System, Processor, Memory, Graphics, Mainboard, Storage, Battery, Network, Information, Success, Warning, Error, Visible, Hidden, Minus, Schedule, Bell, Spark, Person, Tool, Chat };

// The complete set, so every reader of it is answered by the enumeration rather than by a list kept by hand.
class IconCatalog final {
  public:
    [[nodiscard]] static QVector<IconName> allIconNames();
    [[nodiscard]] static QIcon icon(IconName name, const Theme& theme);
    [[nodiscard]] static QIcon icon(IconName name, const QColor& color);
    [[nodiscard]] static QIcon primaryIcon(IconName name, const Theme& theme);
    [[nodiscard]] static QIcon destructiveIcon(IconName name, const Theme& theme);
    [[nodiscard]] static QIcon layoutIcon(const QString& presetId, int columns, int rows, int slotCount, const Theme& theme);
};

} // namespace workpane::ui
