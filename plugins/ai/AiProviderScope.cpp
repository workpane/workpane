#include "AiProviderScope.h"

#include "AiModelConnection.h"
#include "AiProviderCatalog.h"

namespace workpane::plugins::ai {

AiProviderScope::AiProviderScope(QObject* parent) : QObject(parent), m_providerId(ModelConnections::defaultProviderId(ModelEndpoint::Chat)) {}

const QString& AiProviderScope::providerId() const {
    return m_providerId;
}

void AiProviderScope::setProviderId(const QString& providerId) {
    if (providerId == m_providerId) {
        return;
    }

    m_providerId = providerId;
    emit providerChanged(m_providerId);
}

} // namespace workpane::plugins::ai
