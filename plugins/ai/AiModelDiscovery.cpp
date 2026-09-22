#include "AiModelDiscovery.h"

#include "agent/BoundedReply.h"

#include "AiProviderCatalog.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace workpane::plugins::ai {

constexpr qint64 maximumCatalogBytes = 1 << 22;

class AiModelDiscoveryHelper final {
  public:
    static QStringList parseModels(const QJsonObject& payload);
};

QStringList AiModelDiscoveryHelper::parseModels(const QJsonObject& payload) {
    QStringList models;

    for (const auto& value : payload.value(QStringLiteral("data")).toArray()) {
        const QString id = value.toObject().value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && !models.contains(id)) {
            models.append(id);
        }
    }

    models.sort();
    return models;
}

AiModelDiscovery::AiModelDiscovery(QObject* parent) : QObject(parent) {}

bool AiModelDiscovery::running() const {
    return m_reply != nullptr;
}

void AiModelDiscovery::discover(const QString& providerId, const QString& apiKey, const QString& address) {
    if (m_reply != nullptr) {
        return;
    }

    const ProviderDescriptor* provider = ProviderCatalog::findProvider(providerId);

    if (provider == nullptr) {
        emit failed({"ai_provider_unknown", "The provider is unknown", providerId});
        return;
    }

    const auto credential = Secrets::resolveSecret(apiKey);

    if (!credential.hasValue()) {
        emit failed(credential.error());
        return;
    }

    QUrl endpoint(address);
    endpoint.setPath(endpoint.path() + (provider->protocol == WireProtocol::Anthropic ? QStringLiteral("/v1/models") : QStringLiteral("/models")));

    if (!endpoint.isValid() || endpoint.host().isEmpty()) {
        emit failed({"ai_provider_base_url_invalid", "The provider address is invalid", address});
        return;
    }

    QNetworkRequest request(endpoint);
    request.setTransferTimeout(ProviderCatalog::aiLimits().discoveryTimeoutMs);
    request.setRawHeader(QByteArrayLiteral("accept"), QByteArrayLiteral("application/json"));

    for (auto header = provider->httpHeaders.constBegin(); header != provider->httpHeaders.constEnd(); ++header) {
        request.setRawHeader(header.key().toUtf8(), header.value().toUtf8());
    }

    if (provider->protocol == WireProtocol::Anthropic) {
        request.setRawHeader(QByteArrayLiteral("x-api-key"), credential.value().toUtf8());
    } else if (!credential.value().isEmpty()) {
        request.setRawHeader(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + credential.value().toUtf8());
    }

    m_reply = m_network.get(request);
    auto answer = agent::BoundedReplies::boundReply(m_reply, maximumCatalogBytes);
    // clang-format off
    connect(m_reply, &QNetworkReply::finished, this, [this, answer]() {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        answer->bytes.append(reply->read(maximumCatalogBytes - answer->bytes.size()));
        const QByteArray payload = answer->bytes;
        if (!answer->truncated && reply->error() != QNetworkReply::NoError) {
            emit failed({"ai_model_discovery_failed", reply->errorString(), QString::fromUtf8(payload.left(512))});
            return;
        }
        const QStringList models = AiModelDiscoveryHelper::parseModels(QJsonDocument::fromJson(payload).object());
        if (models.isEmpty()) {
            emit failed({"ai_model_discovery_empty", "The provider published no model", {}});
            return;
        }
        emit discovered(models);
    });
    // clang-format on
}

} // namespace workpane::plugins::ai
