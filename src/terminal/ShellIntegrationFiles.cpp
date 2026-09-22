#include "terminal/ShellIntegrationFiles.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

namespace workpane::terminalcore {

class ShellIntegrationFilesHelper final {
  public:
    [[nodiscard]] static bool alreadyHolds(const QString& path, const QByteArray& contents);
    [[nodiscard]] static Result<void> writeFile(const QString& path, const QByteArray& contents);
};

// Windows refuses to replace a file anyone still holds open, so the reader closes before the writer asks for the name.
bool ShellIntegrationFilesHelper::alreadyHolds(const QString& path, const QByteArray& contents) {
    QFile existing(path);
    return existing.open(QIODevice::ReadOnly) && existing.readAll() == contents;
}

// A reader who never changed a startup file keeps its timestamp, because rewriting it on every terminal is a change nobody made.
Result<void> ShellIntegrationFilesHelper::writeFile(const QString& path, const QByteArray& contents) {
    if (alreadyHolds(path, contents)) {
        return Result<void>::success();
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(false);

    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
        file.cancelWriting();
        return Result<void>::failure({"shell_integration_write_failed", "The shell integration could not be written", file.errorString()});
    }

    if (!file.commit()) {
        return Result<void>::failure({"shell_integration_commit_failed", "The shell integration could not be committed", file.errorString()});
    }

    return Result<void>::success();
}

Result<QString> ShellIntegrationFiles::write(const QString& directory, const QString& name, const QList<QPair<QString, QByteArray>>& files) {
    const QString integrationDirectory = QDir(directory).filePath(name);

    if (!QDir().mkpath(integrationDirectory)) {
        return Result<QString>::failure({"shell_integration_directory_failed", "The shell integration directory is unavailable", integrationDirectory});
    }

    for (const auto& [fileName, contents] : files) {
        const auto result = ShellIntegrationFilesHelper::writeFile(QDir(integrationDirectory).filePath(fileName), contents);
        if (!result.hasValue()) {
            return Result<QString>::failure(result.error());
        }
    }

    return Result<QString>::success(integrationDirectory);
}

} // namespace workpane::terminalcore
