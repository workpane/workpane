#include "AiRequestGate.h"

#include <QMetaObject>

#include <algorithm>
#include <functional>
#include <utility>

namespace workpane::plugins::ai {

// The window a requests-per-minute limit is measured over.
constexpr qint64 rateWindowMs = 60000;

AiRequestGate::AiRequestGate(QObject* parent) : QObject(parent) {
    m_clock.start();
}

void AiRequestGate::setLimits(const QVector<ProviderRateLimit>& limits) {
    for (auto provider = m_providers.begin(); provider != m_providers.end(); ++provider) {
        provider->limit = {};
    }

    for (const auto& limit : limits) {
        state(limit.providerId).limit = limit;
    }

    const QStringList providers = m_providers.keys();

    for (const auto& providerId : providers) {
        pump(providerId);
    }
}

AiRequestGate::ProviderState& AiRequestGate::state(const QString& providerId) {
    auto position = m_providers.find(providerId);

    if (position != m_providers.end()) {
        return *position;
    }

    ProviderState created;
    created.timer = new QTimer(this);
    created.timer->setSingleShot(true);
    // clang-format off
    connect(created.timer, &QTimer::timeout, this, [this, providerId]() { pump(providerId); });
    // clang-format on
    return *m_providers.insert(providerId, std::move(created));
}

bool AiRequestGate::concurrencyAvailable(const ProviderState& provider) const {
    return provider.limit.maximumConcurrentRequests == 0 || provider.held.size() < provider.limit.maximumConcurrentRequests;
}

// A zero on any limit means the service was never told to wait for that one.
qint64 AiRequestGate::admissionDelay(const ProviderState& provider, qint64 now) const {
    qint64 delay = 0;

    if (provider.limit.minimumIntervalMs > 0 && provider.lastAdmittedMs >= 0) {
        delay = std::max(delay, provider.lastAdmittedMs + provider.limit.minimumIntervalMs - now);
    }

    if (provider.limit.maximumRequestsPerMinute > 0 && provider.admissions.size() >= provider.limit.maximumRequestsPerMinute) {
        const qint64 oldest = provider.admissions.at(provider.admissions.size() - provider.limit.maximumRequestsPerMinute);
        delay = std::max(delay, oldest + rateWindowMs - now);
    }

    return std::max<qint64>(0, delay);
}

// The slot belongs to the holder from the moment it is counted, so a holder that dies before it hears about it still gives it back.
void AiRequestGate::admit(const QString& providerId, ProviderState& provider, qint64 now) {
    const Waiter waiter = provider.waiters.takeFirst();
    provider.lastAdmittedMs = now;
    provider.admissions.append(now);

    while (!provider.admissions.isEmpty() && provider.admissions.first() + rateWindowMs <= now) {
        provider.admissions.removeFirst();
    }

    QObject* holder = waiter.context;
    // clang-format off
    provider.held.insert(holder, connect(holder, &QObject::destroyed, this, [this, providerId](QObject* dead) { reclaim(providerId, dead); }));
    // clang-format on

    // The admission is delivered on the thread of the caller and never inside the call that asked for it.
    QMetaObject::invokeMethod(holder, waiter.admitted, Qt::QueuedConnection);
}

// A waiter whose caller is gone is dropped instead of being admitted into nothing.
void AiRequestGate::pump(const QString& providerId) {
    auto position = m_providers.find(providerId);

    if (position == m_providers.end()) {
        return;
    }

    ProviderState& provider = *position;

    while (!provider.waiters.isEmpty()) {
        if (provider.waiters.first().context.isNull()) {
            provider.waiters.removeFirst();
            continue;
        }
        if (!concurrencyAvailable(provider)) {
            return;
        }

        const qint64 now = m_clock.elapsed();
        const qint64 delay = admissionDelay(provider, now);
        if (delay > 0) {
            provider.timer->start(static_cast<int>(delay));
            return;
        }
        admit(providerId, provider, now);
    }
}

qint64 AiRequestGate::acquire(const QString& providerId, QObject* context, std::function<void()> admitted) {
    ProviderState& provider = state(providerId);
    provider.waiters.append({context, std::move(admitted)});

    const qint64 now = m_clock.elapsed();
    const qint64 estimate = concurrencyAvailable(provider) ? admissionDelay(provider, now) : -1;
    pump(providerId);
    return estimate;
}

// A holder gives back one slot when it finishes with it, and every slot it still had when it was destroyed.
void AiRequestGate::release(const QString& providerId, QObject* context) {
    auto position = m_providers.find(providerId);

    if (position == m_providers.end()) {
        return;
    }

    const auto held = position->held.find(context);

    if (held == position->held.end()) {
        return;
    }

    disconnect(held.value());
    position->held.erase(held);
    pump(providerId);
}

void AiRequestGate::withdraw(const QString& providerId, QObject* context) {
    auto position = m_providers.find(providerId);

    if (position == m_providers.end()) {
        return;
    }

    // clang-format off
    position->waiters.removeIf([context](const Waiter& waiter) { return waiter.context == context; });
    // clang-format on
}

void AiRequestGate::reclaim(const QString& providerId, QObject* context) {
    auto position = m_providers.find(providerId);

    if (position == m_providers.end() || position->held.remove(context) == 0) {
        return;
    }

    pump(providerId);
}

int AiRequestGate::inFlight(const QString& providerId) const {
    const auto position = m_providers.constFind(providerId);
    return position == m_providers.constEnd() ? 0 : static_cast<int>(position->held.size());
}

int AiRequestGate::waiting(const QString& providerId) const {
    const auto position = m_providers.constFind(providerId);
    return position == m_providers.constEnd() ? 0 : static_cast<int>(position->waiters.size());
}

} // namespace workpane::plugins::ai
