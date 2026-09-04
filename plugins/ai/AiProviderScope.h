#pragma once

#include <QObject>
#include <QString>

namespace workpane::plugins::ai {

// Every per-provider settings section reads the provider this scope carries, so one selector governs all of them.
class AiProviderScope final : public QObject {
    Q_OBJECT

  public:
    explicit AiProviderScope(QObject* parent = nullptr);

    [[nodiscard]] const QString& providerId() const;
    void setProviderId(const QString& providerId);

  signals:
    void providerChanged(const QString& providerId);

  private:
    QString m_providerId;
};

} // namespace workpane::plugins::ai
