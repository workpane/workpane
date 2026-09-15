#pragma once

#include "domain/Result.h"

#include <QHash>
#include <QSqlDatabase>
#include <QString>

namespace workpane::persistence {

inline constexpr int coreDatabaseSchemaVersion = 1;

class CoreDatabaseSchemas final {
  public:
    [[nodiscard]] static const QHash<QString, QString>& coreDatabaseTableSchemas();
    [[nodiscard]] static Result<QHash<QString, int>> validateCoreDatabase(const QSqlDatabase& database);
};

} // namespace workpane::persistence
