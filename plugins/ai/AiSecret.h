#pragma once

#include "domain/Result.h"

#include <QString>

namespace workpane::plugins::ai {

// A secret is either a literal value or a reference to an environment variable written as {env.NAME}.
class Secrets final {
  public:
    [[nodiscard]] static bool isEnvironmentReference(const QString& secret);
    [[nodiscard]] static QString environmentReferenceName(const QString& secret);
    [[nodiscard]] static Result<QString> resolveSecret(const QString& secret);
    [[nodiscard]] static QString defaultSecretReference(const QString& variableName);
};

} // namespace workpane::plugins::ai
