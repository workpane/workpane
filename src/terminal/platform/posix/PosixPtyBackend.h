#pragma once

#include "terminal/platform/IPtyBackend.h"

#include <QByteArray>
#include <QSocketNotifier>
#include <QTimer>

namespace workpane::terminalcore {

class PosixPtyBackend final : public IPtyBackend {
    Q_OBJECT

  public:
    explicit PosixPtyBackend(QObject* parent = nullptr);
    ~PosixPtyBackend() override;

    [[nodiscard]] Result<void> start(const ShellProfile& profile, const QString& workingDirectory, const QString& historyFile, int columns, int rows) override;
    [[nodiscard]] Result<void> write(const QByteArray& bytes) override;
    [[nodiscard]] Result<void> resize(int columns, int rows, int cellWidth, int cellHeight) override;
    void setOutputPaused(bool paused) override;
    void terminate() override;
    [[nodiscard]] bool running() const override;

  private slots:
    void drainOutput();
    void flushInput();
    void checkProcess();
    void refreshWorkingDirectory();

  private:
    void closeDescriptor();
    void releaseNotifier(QSocketNotifier*& notifier);
    void releaseNotifiers();
    void finishProcess(int status);

    int m_descriptor{-1};
    qint64 m_childProcessId{-1};
    QByteArray m_pendingInput;
    QSocketNotifier* m_readNotifier{nullptr};
    QSocketNotifier* m_writeNotifier{nullptr};
    QTimer m_exitTimer;
    QTimer m_directoryTimer;
    QString m_lastWorkingDirectory;
};

} // namespace workpane::terminalcore
