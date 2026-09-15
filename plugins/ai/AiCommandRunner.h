#pragma once

#include "domain/Result.h"

#include <QObject>
#include <QProcess>
#include <QStringDecoder>
#include <QTimer>

#include <functional>

namespace workpane::plugins::ai {

class AiCommandRunner final : public QObject {
    Q_OBJECT

  public:
    explicit AiCommandRunner(QObject* parent = nullptr);
    ~AiCommandRunner() override;

    void start(const QString& command, const QString& workdir, int timeoutSeconds);
    // A prompt reaches an agent exactly as written only when no shell reads it, so a program is started with its arguments.
    void startProgram(const QString& program, const QStringList& arguments, const QString& workdir, int timeoutSeconds, const QStringList& clearedVariables = {});
    void cancel();
    [[nodiscard]] bool running() const;

  signals:
    void outputReceived(const QString& text);
    void finished(int exitCode, const QString& output);
    void failed(const Error& error);

  private:
    void launch(const QString& program, const QStringList& arguments, const QString& workdir, int timeoutSeconds, const QStringList& clearedVariables);
    void readOutput();
    void stopProcess();
    void completeProcess(int exitCode, QProcess::ExitStatus status);
    void reportFailure(const Error& error);
    void release();

    QProcess* m_process{nullptr};
    QTimer m_timeout;
    QString m_output;
    // A character of the output may be split across two reads, so the decoding keeps what it could not finish yet.
    QStringDecoder m_decoder{QStringDecoder::Utf8};
    QString m_pendingControl;
    bool m_completed{false};
    bool m_timedOut{false};
};

class CommandOutput final {
  public:
    // A shell writes colors, cursor moves and progress rewrites, so the readable text is what reaches the execution record.
    [[nodiscard]] static QString plainCommandOutput(QString& pending, const QString& chunk);
    // A card shows the sentence of the catalog while the diagnostic of the failure stays in the log.
    [[nodiscard]] static QString commandFailureMessage(const Error& error, const std::function<QString(const QString&)>& translate);
};

} // namespace workpane::plugins::ai
