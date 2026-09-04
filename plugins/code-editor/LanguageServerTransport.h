#pragma once

#include "LanguageRegistry.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringDecoder>

namespace workpane::plugins::codeeditor {

// The process, the framing and the JSON of a language server live on their own thread, because a payload of any size must never be read or written where the interface runs.
class LanguageServerTransport final : public QObject {
    Q_OBJECT

  public:
    LanguageServerTransport(ResolvedLanguageServer server, QString rootPath);
    ~LanguageServerTransport() override;

  public slots:
    void start();
    void send(const QJsonObject& message);
    void requestTermination();
    void kill();
    void shutdown();

  signals:
    void started();
    void messageReceived(const QJsonObject& message);
    void diagnosticText(const QString& text);
    void startFailed(const QString& message);
    void protocolFailed(const QString& message);
    void exited(int exitCode);

  private:
    void readOutput();
    void parseMessages();
    void abort(const QString& message);
    [[nodiscard]] bool readHeader(qsizetype headerEnd, qsizetype& contentLength);

    ResolvedLanguageServer m_server;
    QString m_rootPath;
    QProcess* m_process{nullptr};
    QByteArray m_inputBuffer;
    // A character of the diagnostic stream may be split across two reads, so the decoding keeps what it could not finish yet.
    QStringDecoder m_diagnosticDecoder{QStringDecoder::Utf8};
    bool m_aborted{false};
    bool m_startFailureReported{false};
};

} // namespace workpane::plugins::codeeditor
