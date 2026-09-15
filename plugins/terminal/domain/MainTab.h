#pragma once

#include "domain/SlotLayoutState.h"

#include <QColor>
#include <QString>

namespace workpane::domain {

struct MainTab final {
    QString id;
    QString workspaceId;
    QString name;
    int sortOrder{};
    QColor accentColor;
    QString focusedSessionId;
    SlotLayoutState layout;
};

} // namespace workpane::domain
