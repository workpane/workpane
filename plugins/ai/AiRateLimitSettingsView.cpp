#include "AiRateLimitSettingsView.h"

#include "AiPlugin.h"
#include "AiProviderCatalog.h"
#include "ui/Components.h"
#include "ui/Theme.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace workpane::plugins::ai {

AiRateLimitSettingsView::AiRateLimitSettingsView(AiPlugin& plugin, PluginHost& host, AiProviderScope& scope, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_host(host), m_scope(scope) {
    setObjectName(QStringLiteral("aiRateLimitSettings"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const auto [page, layout] = ui::Components::settingsSectionPage(this);
    auto* form = ui::Components::settingsForm();

    m_interval = new QSpinBox(page);
    m_interval->setObjectName(QStringLiteral("aiRateLimitInterval"));
    m_interval->setRange(0, ProviderCatalog::aiLimits().maximumRequestDelayMs);
    m_perMinute = new QSpinBox(page);
    m_perMinute->setObjectName(QStringLiteral("aiRateLimitPerMinute"));
    m_perMinute->setRange(0, ProviderCatalog::aiLimits().maximumRequestsPerMinute);
    m_concurrent = new QSpinBox(page);
    m_concurrent->setObjectName(QStringLiteral("aiRateLimitConcurrent"));
    m_concurrent->setRange(0, ProviderCatalog::aiLimits().maximumConcurrentRequests);

    ui::Components::addSettingsRow(form, m_host.translate(QStringLiteral("ai.settings.rate-limit-interval")), ui::Components::stepperRow(m_interval, m_host.theme(), page));
    ui::Components::addSettingsRow(form, m_host.translate(QStringLiteral("ai.settings.rate-limit-per-minute")), ui::Components::stepperRow(m_perMinute, m_host.theme(), page));
    ui::Components::addSettingsRow(form, m_host.translate(QStringLiteral("ai.settings.rate-limit-concurrent")), ui::Components::stepperRow(m_concurrent, m_host.theme(), page));
    layout->addLayout(form);
    layout->addStretch(1);
    root->addWidget(page, 1);

    // clang-format off
    connect(&m_scope, &AiProviderScope::providerChanged, this, [this]() { applyProvider(); });
    connect(m_interval, &QSpinBox::valueChanged, this, [this]() { persist(); });
    connect(m_perMinute, &QSpinBox::valueChanged, this, [this]() { persist(); });
    connect(m_concurrent, &QSpinBox::valueChanged, this, [this]() { persist(); });
    // clang-format on
    applyProvider();
}

// The fields present what the selected provider was given, and a provider nobody limited opens at the unlimited zero.
void AiRateLimitSettingsView::applyProvider() {
    const QString providerId = m_scope.providerId();
    const QVector<ProviderRateLimit> stored = m_plugin.rateLimits();
    // clang-format off
    const auto found = std::find_if(stored.constBegin(), stored.constEnd(), [&providerId](const ProviderRateLimit& limit) { return limit.providerId == providerId; });
    // clang-format on
    const ProviderRateLimit limit = found == stored.constEnd() ? ProviderRateLimit{providerId, 0, 0, 0} : *found;

    m_loading = true;
    m_interval->setValue(limit.minimumIntervalMs);
    m_perMinute->setValue(limit.maximumRequestsPerMinute);
    m_concurrent->setValue(limit.maximumConcurrentRequests);
    m_loading = false;
}

// A provider the user left unlimited is not stored, because a row of zeros is what the absence already means.
void AiRateLimitSettingsView::persist() {
    if (m_loading) {
        return;
    }

    const QString providerId = m_scope.providerId();
    const ProviderRateLimit edited{providerId, m_interval->value(), m_perMinute->value(), m_concurrent->value()};
    QVector<ProviderRateLimit> limits;

    for (const auto& limit : m_plugin.rateLimits()) {
        if (limit.providerId != providerId) {
            limits.append(limit);
        }
    }

    if (edited.minimumIntervalMs > 0 || edited.maximumRequestsPerMinute > 0 || edited.maximumConcurrentRequests > 0) {
        limits.append(edited);
    }

    auto future = m_plugin.saveRateLimits(limits);
    // clang-format off
    const auto reported = [this](Result<void> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), m_host.translate(QStringLiteral("ai.error.settings-save")), AlertSeverity::Error); } };
    // clang-format on
    future.then(this, reported);
}

} // namespace workpane::plugins::ai
