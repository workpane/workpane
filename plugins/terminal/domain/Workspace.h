#pragma once

#include "domain/MainTab.h"
#include "domain/TerminalSessionState.h"

#include <QString>
#include <QVector>
#include <QtTypes>

namespace workpane::domain {

struct Workspace final {
    QString id;
    QString name;
    qint64 createdAt{};
    qint64 updatedAt{};
    qint64 lastOpenedAt{};
    QString selectedMainTabId;
    QVector<MainTab> tabs;
    QVector<TerminalSessionState> sessions;
};

} // namespace workpane::domain
