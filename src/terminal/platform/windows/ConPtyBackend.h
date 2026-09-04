#pragma once

#include "terminal/platform/IPtyBackend.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace workpane::terminalcore {

class ConPtyBackend final : public IPtyBackend {
    Q_OBJECT

  public:
    explicit ConPtyBackend(QObject* parent = nullptr);
    ~ConPtyBackend() override;

    [[nodiscard]] Result<void> start(const ShellProfile& profile, const QString& workingDirectory, const QString& historyFile, int columns, int rows) override;
    [[nodiscard]] Result<void> write(const QByteArray& bytes) override;
    [[nodiscard]] Result<void> resize(int columns, int rows, int cellWidth, int cellHeight) override;
    void setOutputPaused(bool paused) override;
    void terminate() override;
    [[nodiscard]] bool running() const override;

  private:
    struct Handles;
    void readOutput();
    void writeInput();
    static unsigned long __stdcall readerEntry(void* backend);
    static unsigned long __stdcall writerEntry(void* backend);

    std::unique_ptr<Handles> m_handles;
    std::atomic_bool m_stopRequested{false};
    std::mutex m_inputMutex;
    std::condition_variable m_inputCondition;
    std::mutex m_outputMutex;
    std::condition_variable m_outputCondition;
    QByteArray m_pendingInput;
    std::atomic_bool m_running{false};
    std::atomic_bool m_stopping{false};
    std::atomic_bool m_outputPaused{false};
};

} // namespace workpane::terminalcore
