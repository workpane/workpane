#include "AiSecret.h"

#include <QRegularExpression>

namespace workpane::plugins::ai {

class AiSecretHelper final {
  public:
    static const QRegularExpression& environmentReferencePattern();
};

const QRegularExpression& AiSecretHelper::environmentReferencePattern() {
    static const QRegularExpression pattern(QStringLiteral("^\\{env\\.([A-Za-z_][A-Za-z0-9_]*)\\}$"));
    return pattern;
}

bool Secrets::isEnvironmentReference(const QString& secret) {
    return AiSecretHelper::environmentReferencePattern().match(secret).hasMatch();
}

QString Secrets::environmentReferenceName(const QString& secret) {
    const auto match = AiSecretHelper::environmentReferencePattern().match(secret);
    return match.hasMatch() ? match.captured(1) : QString{};
}

Result<QString> Secrets::resolveSecret(const QString& secret) {
    if (!Secrets::isEnvironmentReference(secret)) {
        return Result<QString>::success(secret);
    }

    const QString name = Secrets::environmentReferenceName(secret);
    const QString value = qEnvironmentVariable(name.toLatin1().constData());

    if (value.isEmpty()) {
        return Result<QString>::failure({"ai_secret_environment_missing", "The referenced environment variable is not set", name});
    }

    return Result<QString>::success(value);
}

// A credential normally lives in the environment, so a form starts from the reference the service officially documents.
QString Secrets::defaultSecretReference(const QString& variableName) {
    return variableName.isEmpty() ? QString{} : QStringLiteral("{env.%1}").arg(variableName);
}

} // namespace workpane::plugins::ai
