#include "agent/BoundedReply.h"

#include <QNetworkReply>

namespace workpane::agent {

std::shared_ptr<BoundedReply> BoundedReplies::boundReply(QNetworkReply* reply, qsizetype maximumBytes) {
    auto answer = std::make_shared<BoundedReply>();
    // clang-format off
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, answer, maximumBytes]() {
        answer->bytes.append(reply->read(maximumBytes - answer->bytes.size()));
        if (answer->bytes.size() >= maximumBytes) {
            answer->truncated = true;
            reply->abort();
        }
    });
    // clang-format on
    return answer;
}

} // namespace workpane::agent
