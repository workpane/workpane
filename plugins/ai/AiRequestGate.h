#pragma once

#include "AiTaskRepository.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMultiHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>

namespace workpane::plugins::ai {

// Every request to one provider passes through the same gate, so the tasks of every workspace share the pace that service allows.
class AiRequestGate final : public QObject {
    Q_OBJECT

  public:
    explicit AiRequestGate(QObject* parent = nullptr);

    void setLimits(const QVector<ProviderRateLimit>& limits);
    // The admission is held until it is released, and the estimated wait is returned so the caller can say why nothing is happening.
    qint64 acquire(const QString& providerId, QObject* context, std::function<void()> admitted);
    void release(const QString& providerId, QObject* context);
    // A caller that stops before its turn came gives back the place it was holding in the queue.
    void withdraw(const QString& providerId, QObject* context);
    [[nodiscard]] int inFlight(const QString& providerId) const;
    [[nodiscard]] int waiting(const QString& providerId) const;

  private:
    struct Waiter final {
        QPointer<QObject> context;
        std::function<void()> admitted;
    };

    struct ProviderState final {
        ProviderRateLimit limit;
        QMultiHash<QObject*, QMetaObject::Connection> held;
        qint64 lastAdmittedMs{-1};
        QList<qint64> admissions;
        QList<Waiter> waiters;
        QTimer* timer{nullptr};
    };

    [[nodiscard]] ProviderState& state(const QString& providerId);
    [[nodiscard]] qint64 admissionDelay(const ProviderState& provider, qint64 now) const;
    [[nodiscard]] bool concurrencyAvailable(const ProviderState& provider) const;
    void admit(const QString& providerId, ProviderState& provider, qint64 now);
    void pump(const QString& providerId);
    void reclaim(const QString& providerId, QObject* context);

    QElapsedTimer m_clock;
    QHash<QString, ProviderState> m_providers;
};

} // namespace workpane::plugins::ai
