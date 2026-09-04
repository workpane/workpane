#include "StaticFileResolver.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QUrl>

#include <optional>

namespace workpane::plugins::webserver {

constexpr qint64 maximumStaticFileSize = 256LL * 1024 * 1024;

StaticFileResolver::StaticFileResolver(const QString& documentRoot) : m_canonicalRoot(QFileInfo(documentRoot).canonicalFilePath()) {}

bool StaticFileResolver::valid() const {
    const QFileInfo root(m_canonicalRoot);
    return !m_canonicalRoot.isEmpty() && root.isDir() && root.isReadable();
}

std::optional<StaticFile> StaticFileResolver::resolve(const QByteArray& encodedPath) const {
    if (m_canonicalRoot.isEmpty() || encodedPath.contains('\0') || encodedPath.size() > 8192) {
        return std::nullopt;
    }

    const QString decoded = QUrl::fromPercentEncoding(encodedPath);

    if (decoded.contains(QChar::Null) || decoded.contains(QLatin1Char('\\'))) {
        return std::nullopt;
    }

    QString relativePath = decoded;

    while (relativePath.startsWith(QLatin1Char('/'))) {
        relativePath.removeFirst();
    }

    const QString cleanPath = QDir::cleanPath(relativePath);

    if (cleanPath == QStringLiteral("..") || cleanPath.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(cleanPath)) {
        return std::nullopt;
    }

    QFileInfo candidate(QDir(m_canonicalRoot).filePath(cleanPath));

    if (candidate.isDir()) {
        const QDir directory(candidate.absoluteFilePath());
        const QFileInfo htmlIndex(directory.filePath(QStringLiteral("index.html")));
        const QFileInfo htmIndex(directory.filePath(QStringLiteral("index.htm")));
        candidate = htmlIndex.isFile() ? htmlIndex : htmIndex;
    }

    const QString canonicalCandidate = candidate.canonicalFilePath();
    const QString relativeCandidate = QDir(m_canonicalRoot).relativeFilePath(canonicalCandidate);

    if (canonicalCandidate.isEmpty() || relativeCandidate == QStringLiteral("..") || relativeCandidate.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relativeCandidate) || !candidate.isFile() || !candidate.isReadable() || candidate.size() < 0 || candidate.size() > maximumStaticFileSize) {
        return std::nullopt;
    }

    return StaticFile{canonicalCandidate, mimeType(canonicalCandidate), candidate.size()};
}

QByteArray StaticFileResolver::mimeType(const QString& fileName) {
    const QMimeType type = QMimeDatabase().mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);
    return type.isValid() ? type.name().toLatin1() : QByteArrayLiteral("application/octet-stream");
}

} // namespace workpane::plugins::webserver
