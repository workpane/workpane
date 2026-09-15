#pragma once

#include "AiSecret.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

namespace workpane::plugins::ai {

// A provider that publishes its catalog answers the models endpoint, so the selectable models come from the service itself.
class AiModelDiscovery final : public QObject {
    Q_OBJECT

  public:
    explicit AiModelDiscovery(QObject* parent = nullptr);

    void discover(const QString& providerId, const QString& apiKey, const QString& address);
    [[nodiscard]] bool running() const;

  signals:
    void discovered(const QStringList& models);
    void failed(const Error& error);

  private:
    QNetworkAccessManager m_network;
    QNetworkReply* m_reply{nullptr};
};

} // namespace workpane::plugins::ai
