#include "RequestLogModel.h"

#include <QReadLocker>
#include <QWriteLocker>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace workpane::plugins::webserver {

RequestLogModel::RequestLogModel(qsizetype capacity) : m_capacity(capacity) {}

void RequestLogModel::append(RequestLogEntry entry) {
    const QWriteLocker locker(&m_lock);
    entry.sequence = m_nextSequence++;

    while (!m_entries.isEmpty() && m_entries.size() >= m_capacity) {
        m_entries.removeFirst();
    }

    if (m_entries.size() < m_capacity) {
        m_entries.append(std::move(entry));
    }
}

void RequestLogModel::clear() {
    const QWriteLocker locker(&m_lock);
    m_entries.clear();
}

RequestLogBatch RequestLogModel::entriesSince(std::uint64_t cursor, qsizetype maximumEntries) const {
    const QReadLocker locker(&m_lock);
    RequestLogBatch batch;
    batch.cursor = cursor;
    batch.entries.reserve(std::min(maximumEntries, m_entries.size()));

    for (const auto& entry : m_entries) {
        if (entry.sequence <= cursor) {
            continue;
        }

        batch.entries.append(entry);
        batch.cursor = entry.sequence;
        if (batch.entries.size() == maximumEntries) {
            break;
        }
    }

    return batch;
}

} // namespace workpane::plugins::webserver
