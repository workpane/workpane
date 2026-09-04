#pragma once

#include <QByteArray>

#include <memory>

class QNetworkReply;

namespace workpane::agent {

struct BoundedReply final {
    QByteArray bytes;
    bool truncated{false};
};

// The size of an answer is decided by whoever sends it, so the bound holds while the bytes arrive rather than once they all have.
class BoundedReplies final {
  public:
    [[nodiscard]] static std::shared_ptr<BoundedReply> boundReply(QNetworkReply* reply, qsizetype maximumBytes);
};

} // namespace workpane::agent
