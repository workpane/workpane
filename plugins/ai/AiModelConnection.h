#pragma once

#include "AiProviderCatalog.h"
#include "AiSecret.h"
#include "domain/Result.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace workpane::plugins::ai {

// A connection is one configured provider and model pair, identified by the key a task stores.
struct ModelConnection final {
    QString providerId;
    QString modelId;
    QString displayName;
    QString apiKey;
    QString address;
    QJsonObject parameters;
    // What the user declares beyond the parameters this project knows, merged over them when the request is built.
    QJsonObject extraParameters;
};

struct ResolvedEndpoint final {
    QString url;
    QString apiKeyVariable;
    QMap<QString, QString> httpHeaders;
};

class ModelConnections final {
  public:
    [[nodiscard]] static QString connectionKey(const QString& providerId, const QString& modelId);
    [[nodiscard]] static QString connectionKey(const ModelConnection& connection);
    [[nodiscard]] static QString connectionLabel(const ModelConnection& connection);
    [[nodiscard]] static QString connectionAddress(const ModelConnection& connection);
    // Every modality is reached through this, so a provider that answers one is data and never a path written into a tool.
    [[nodiscard]] static std::optional<ResolvedEndpoint> resolveEndpoint(const QString& providerId, const QString& address, ModelEndpoint endpoint);
    [[nodiscard]] static QVector<const ProviderDescriptor*> providersAnswering(ModelEndpoint endpoint);
    // The catalog decides which providers answer a modality, so the first of them is what a surface opens on before anyone chooses.
    [[nodiscard]] static QString defaultProviderId(ModelEndpoint endpoint);
    [[nodiscard]] static const ModelConnection* findConnection(const QVector<ModelConnection>& connections, const QString& key);
    [[nodiscard]] static Result<QJsonObject> validateParameters(const ProviderDescriptor& provider, const QString& modelId, const QJsonObject& parameters);
    [[nodiscard]] static Result<QJsonObject> validateExtraParameters(const QJsonObject& parameters);
    [[nodiscard]] static Result<ModelConnection> validateConnection(const ModelConnection& connection);
    [[nodiscard]] static Result<void> validateConnectionSet(const QVector<ModelConnection>& connections);
    [[nodiscard]] static ModelConnection declaredConnection(const ProviderDescriptor& provider, const QString& modelId);
    [[nodiscard]] static WireProtocol connectionProtocol(const ModelConnection& connection);
    [[nodiscard]] static qint64 outputBudget(const ModelConnection& connection);
    static void setOutputBudget(ModelConnection& connection, qint64 tokens);
};

} // namespace workpane::plugins::ai
