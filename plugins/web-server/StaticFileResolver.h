#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace workpane::plugins::webserver {

struct StaticFile final {
    QString canonicalPath;
    QByteArray mimeType;
    qint64 size{};
};

class StaticFileResolver final {
  public:
    explicit StaticFileResolver(const QString& documentRoot);

    [[nodiscard]] bool valid() const;
    [[nodiscard]] std::optional<StaticFile> resolve(const QByteArray& encodedPath) const;
    [[nodiscard]] static QByteArray mimeType(const QString& fileName);

  private:
    QString m_canonicalRoot;
};

} // namespace workpane::plugins::webserver
