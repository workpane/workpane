#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace workpane::persistence {

struct DatabaseMigration final {
    int version{0};
    QStringList statements;
};

struct DatabaseStatement final {
    QString statement;
    QVariantList bindings;
};

using DatabaseRows = QVector<QVariantMap>;

} // namespace workpane::persistence
