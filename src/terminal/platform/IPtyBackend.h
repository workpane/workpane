#pragma once

#include "domain/Result.h"
#include "terminal/ShellProfile.h"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace workpane::terminalcore {

class IPtyBackend : public QObject {
    Q_OBJECT

  public:
    explicit IPtyBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~IPtyBackend() override = default;

    [[nodiscard]] virtual Result<void> start(const ShellProfile& profile, const QString& workingDirectory, const QString& historyFile, int columns, int rows) = 0;
    [[nodiscard]] virtual Result<void> write(const QByteArray& bytes) = 0;
    [[nodiscard]] virtual Result<void> resize(int columns, int rows, int cellWidth, int cellHeight) = 0;
    virtual void setOutputPaused(bool paused) = 0;
    virtual void terminate() = 0;
    [[nodiscard]] virtual bool running() const = 0;

  signals:
    void outputReady(const QByteArray& bytes);
    void workingDirectoryChanged(const QString& directory);
    void processExited(int exitCode);
    void backendError(const QString& message);
};

} // namespace workpane::terminalcore
