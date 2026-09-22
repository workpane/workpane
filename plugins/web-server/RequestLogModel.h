#pragma once

#include <QDateTime>
#include <QReadWriteLock>
#include <QString>
#include <QVector>

#include <cstdint>

namespace workpane::plugins::webserver {

struct RequestLogEntry final {
    std::uint64_t sequence{};
    QDateTime timestamp;
    QString method;
    QString path;
    int status{};
    qint64 durationMilliseconds{};
    qint64 responseBytes{};
    QString remoteAddress;
};

struct RequestLogBatch final {
    std::uint64_t cursor{};
    QVector<RequestLogEntry> entries;
};

class RequestLogModel final {
  public:
    explicit RequestLogModel(qsizetype capacity = 1000);

    void append(RequestLogEntry entry);
    void clear();
    [[nodiscard]] RequestLogBatch entriesSince(std::uint64_t cursor, qsizetype maximumEntries) const;

  private:
    qsizetype m_capacity;
    mutable QReadWriteLock m_lock;
    QVector<RequestLogEntry> m_entries;
    std::uint64_t m_nextSequence{1};
};

} // namespace workpane::plugins::webserver
