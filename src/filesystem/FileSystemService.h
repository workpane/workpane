#pragma once

#include "domain/Result.h"

#include <QByteArray>
#include <QFuture>
#include <QThreadPool>
#include <QVector>

namespace workpane::filesystem {

// What a directory holds is decided by whoever filled it, so a listing is answered away from the thread that draws and bounded by what the caller asked for.
struct DirectoryEntry final {
    QString name;
    bool directory{false};
    qint64 size{0};

    [[nodiscard]] bool operator==(const DirectoryEntry& other) const = default;
};

class FileSystemService final {
  public:
    FileSystemService();
    ~FileSystemService();

    [[nodiscard]] QFuture<Result<QByteArray>> readFile(const QString& path, qint64 maximumBytes);
    [[nodiscard]] QFuture<Result<QVector<DirectoryEntry>>> listDirectory(const QString& path, int maximumEntries);
    [[nodiscard]] QFuture<Result<void>> writeFile(const QString& path, const QByteArray& content);
    [[nodiscard]] QFuture<Result<void>> createFile(const QString& path);
    [[nodiscard]] QFuture<Result<void>> createDirectory(const QString& path);
    [[nodiscard]] QFuture<Result<void>> movePath(const QString& sourcePath, const QString& destinationPath);
    [[nodiscard]] QFuture<Result<void>> copyFile(const QString& sourcePath, const QString& destinationPath);
    [[nodiscard]] QFuture<Result<void>> removeFile(const QString& path);
    [[nodiscard]] QFuture<Result<void>> removeDirectory(const QString& path);
    void drain();

  private:
    QThreadPool m_pool;
};

} // namespace workpane::filesystem
