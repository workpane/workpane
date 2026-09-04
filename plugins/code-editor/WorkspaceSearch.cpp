#include "WorkspaceSearch.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>

namespace workpane::plugins::codeeditor {

constexpr int maximumMatchTextLength = 400;

class WorkspaceSearchHelper final {
  public:
    static bool searchable(const QFileInfo& file, qint64 maximumFileBytes);
    static QString decoded(const QByteArray& content);
};

bool WorkspaceSearchHelper::searchable(const QFileInfo& file, qint64 maximumFileBytes) {
    return file.isFile() && !file.isSymLink() && file.size() > 0 && file.size() <= maximumFileBytes;
}

// A file whose bytes are not text is skipped rather than answered with the character that stands for what the decoding lost.
QString WorkspaceSearchHelper::decoded(const QByteArray& content) {
    if (content.contains('\0')) {
        return {};
    }

    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = decoder(content);
    return decoder.hasError() ? QString{} : text;
}

WorkspaceSearchResult WorkspaceSearches::searchWorkspace(const QString& rootPath, const QString& query, qint64 maximumFileBytes, int maximumMatches) {
    WorkspaceSearchResult result;

    if (query.isEmpty() || maximumMatches <= 0) {
        return result;
    }

    const QDir root(rootPath);
    QDirIterator entries(rootPath, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

    while (entries.hasNext()) {
        const QString path = entries.next();

        if (path.contains(QStringLiteral("/.git/")) || !WorkspaceSearchHelper::searchable(entries.fileInfo(), maximumFileBytes)) {
            continue;
        }

        QFile file(path);

        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }

        const QString text = WorkspaceSearchHelper::decoded(file.readAll());
        file.close();

        if (text.isEmpty()) {
            continue;
        }

        const QString relative = root.relativeFilePath(path);
        int line = 0;

        for (const auto& content : QStringView(text).split(QLatin1Char('\n'))) {
            if (content.contains(query, Qt::CaseInsensitive)) {
                if (result.matches.size() >= maximumMatches) {
                    result.complete = false;
                    return result;
                }

                result.matches.append({relative, line, content.trimmed().left(maximumMatchTextLength).toString()});
            }

            ++line;
        }
    }

    return result;
}

} // namespace workpane::plugins::codeeditor
