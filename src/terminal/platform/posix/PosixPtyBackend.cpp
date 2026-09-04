#include "terminal/platform/posix/PosixPtyBackend.h"

#include "terminal/TerminalDimensions.h"
#include "terminal/platform/posix/PosixShellIntegration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <fcntl.h>
#ifdef Q_OS_MACOS
#include <libproc.h>
#include <util.h>
#else
#include <pty.h>
#endif
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace workpane::terminalcore {

constexpr qsizetype maximumInputQueueSize = 1024 * 1024;
constexpr qsizetype maximumOutputBatchSize = 64 * 1024;
constexpr auto processReapInterval = std::chrono::milliseconds(10);
constexpr auto gracefulTerminationInterval = std::chrono::milliseconds(100);

class ChildProcessReaper final {
  public:
    static ChildProcessReaper& instance() {
        static ChildProcessReaper reaper;
        return reaper;
    }

    [[nodiscard]] bool ensureRunning() {
        const std::lock_guard lock(m_mutex);

        if (m_started) {
            return true;
        }
        if (::pthread_create(&m_worker, nullptr, &ChildProcessReaper::entry, this) != 0) {
            return false;
        }

        m_started = true;
        return true;
    }

    void schedule(pid_t processId) {
        {
            const std::lock_guard lock(m_mutex);
            m_pending.push_back({processId, std::chrono::steady_clock::now() + gracefulTerminationInterval});
        }
        m_condition.notify_one();
    }

  private:
    struct Child final {
        pid_t processId{};
        std::chrono::steady_clock::time_point forceTerminationAt;
        bool forceTerminationSent{false};
    };

    ChildProcessReaper() = default;

    ~ChildProcessReaper() {
        {
            const std::lock_guard lock(m_mutex);
            m_stopRequested.store(true, std::memory_order_release);
        }
        m_condition.notify_all();

        if (m_started) {
            ::pthread_join(m_worker, nullptr);
        }
    }

    static void* entry(void* reaper) {
        static_cast<ChildProcessReaper*>(reaper)->run();
        return nullptr;
    }

    void run() {
        std::vector<Child> children;

        for (;;) {
            {
                std::unique_lock lock(m_mutex);
                if (children.empty() && m_pending.empty()) {
                    // clang-format off
                    m_condition.wait(lock, [this]() { return m_stopRequested.load(std::memory_order_acquire) || !m_pending.empty(); });
                    // clang-format on
                }
                children.insert(children.end(), m_pending.begin(), m_pending.end());
                m_pending.clear();
            }

            if (m_stopRequested.load(std::memory_order_acquire)) {
                terminateAndReap(children);
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            // clang-format off
            const auto hasExited = [now](Child& child) {
                int status = 0;
                const pid_t result = ::waitpid(child.processId, &status, WNOHANG);
                if (result > 0 || (result < 0 && errno == ECHILD)) {
                    return true;
                }
                if (now >= child.forceTerminationAt && !child.forceTerminationSent) {
                    ::kill(-child.processId, SIGKILL);
                    ::kill(child.processId, SIGKILL);
                    child.forceTerminationSent = true;
                }
                return false;
            };
            // clang-format on
            std::erase_if(children, hasExited);

            std::unique_lock lock(m_mutex);
            // clang-format off
            m_condition.wait_for(lock, processReapInterval, [this]() { return m_stopRequested.load(std::memory_order_acquire) || !m_pending.empty(); });
            // clang-format on
        }
    }

    void terminateAndReap(std::vector<Child>& children) {
        {
            const std::lock_guard lock(m_mutex);
            children.insert(children.end(), m_pending.begin(), m_pending.end());
            m_pending.clear();
        }

        for (const auto& child : children) {
            ::kill(-child.processId, SIGKILL);
            ::kill(child.processId, SIGKILL);
            int status = 0;
            while (::waitpid(child.processId, &status, 0) < 0 && errno == EINTR) {}
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<Child> m_pending;
    std::atomic_bool m_stopRequested{false};
    pthread_t m_worker{};
    bool m_started{false};
};

class PosixPtyBackendHelper final {
  public:
    static QString errorText(int number);
    static Result<void> systemFailure(const QString& code, const QString& message);
    static QString processWorkingDirectory(pid_t processId);
};

// The description is written into a buffer of our own, because the one strerror returns is shared by the whole process.
QString PosixPtyBackendHelper::errorText(int number) {
    std::array<char, 256> buffer{};

    if (::strerror_r(number, buffer.data(), buffer.size()) != 0) {
        return QString::number(number);
    }

    return QString::fromLocal8Bit(buffer.data());
}

Result<void> PosixPtyBackendHelper::systemFailure(const QString& code, const QString& message) {
    return Result<void>::failure({code, message, errorText(errno)});
}

QString PosixPtyBackendHelper::processWorkingDirectory(pid_t processId) {
#ifdef Q_OS_MACOS
    proc_vnodepathinfo information{};
    const int size = proc_pidinfo(processId, PROC_PIDVNODEPATHINFO, 0, &information, static_cast<int>(sizeof(information)));

    if (size != static_cast<int>(sizeof(information))) {
        return {};
    }

    return QFile::decodeName(information.pvi_cdir.vip_path);
#else
    return QFileInfo(QStringLiteral("/proc/%1/cwd").arg(processId)).symLinkTarget();
#endif
}

PosixPtyBackend::PosixPtyBackend(QObject* parent) : IPtyBackend(parent) {
    m_exitTimer.setInterval(50);
    m_directoryTimer.setInterval(500);
    connect(&m_exitTimer, &QTimer::timeout, this, &PosixPtyBackend::checkProcess);
    connect(&m_directoryTimer, &QTimer::timeout, this, &PosixPtyBackend::refreshWorkingDirectory);
}

PosixPtyBackend::~PosixPtyBackend() {
    terminate();
}

Result<void> PosixPtyBackend::start(const ShellProfile& profile, const QString& workingDirectory, const QString& historyFile, int columns, int rows) {
    if (running()) {
        return Result<void>::failure({"terminal_already_running", "The terminal process is already running", {}});
    }
    if (!QFileInfo(profile.executable).isExecutable()) {
        return Result<void>::failure({"shell_not_executable", "The selected shell is not executable", profile.executable});
    }
    if (!QFileInfo(workingDirectory).isDir()) {
        return Result<void>::failure({"terminal_working_directory_missing", "The working directory does not exist", workingDirectory});
    }
    if (!TerminalDimensions::validTerminalGrid(columns, rows)) {
        return Result<void>::failure({"terminal_size_invalid", "The terminal dimensions are invalid", QStringLiteral("%1x%2").arg(columns).arg(rows)});
    }
    if (!ChildProcessReaper::instance().ensureRunning()) {
        return Result<void>::failure({"terminal_thread_unavailable", "The system refused a thread this terminal needs", {}});
    }

    const QByteArray executable = QFile::encodeName(profile.executable);
    const QByteArray executableName = QFileInfo(profile.executable).fileName().toLocal8Bit();
    const QByteArray cwd = QFile::encodeName(workingDirectory);
    // clang-format off
    const auto encodedArguments = [&profile]() {
        QList<QByteArray> arguments;
        arguments.reserve(profile.arguments.size());
        for (const auto& argument : profile.arguments) {
            arguments.append(argument.toLocal8Bit());
        }
        return arguments;
    }();
    // clang-format on

    std::vector<char*> arguments;
    arguments.reserve(static_cast<std::size_t>(encodedArguments.size()) + 2);
    arguments.push_back(const_cast<char*>(executableName.constData()));

    for (const auto& argument : encodedArguments) {
        arguments.push_back(const_cast<char*>(argument.constData()));
    }

    arguments.push_back(nullptr);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    environment.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("Workpane"));
    const auto integrationResult = PosixShellIntegration::configure(profile, historyFile, environment);

    if (!integrationResult.hasValue()) {
        return integrationResult;
    }

    const QStringList environmentStrings = environment.toStringList();
    std::vector<QByteArray> encodedEnvironment;
    encodedEnvironment.reserve(static_cast<std::size_t>(environmentStrings.size()));

    for (const auto& value : environmentStrings) {
        encodedEnvironment.push_back(value.toLocal8Bit());
    }

    std::vector<char*> environmentPointers;
    environmentPointers.reserve(encodedEnvironment.size() + 1);

    for (auto& value : encodedEnvironment) {
        environmentPointers.push_back(value.data());
    }

    environmentPointers.push_back(nullptr);

    winsize size{};
    size.ws_col = static_cast<unsigned short>(columns);
    size.ws_row = static_cast<unsigned short>(rows);

    const pid_t child = forkpty(&m_descriptor, nullptr, nullptr, &size);

    if (child < 0) {
        return PosixPtyBackendHelper::systemFailure(QStringLiteral("terminal_spawn_failed"), QStringLiteral("The shell process could not be started"));
    }

    if (child == 0) {
        if (::chdir(cwd.constData()) != 0) {
            _exit(126);
        }

        ::execve(executable.constData(), arguments.data(), environmentPointers.data());
        _exit(127);
    }

    m_childProcessId = child;
    const int flags = ::fcntl(m_descriptor, F_GETFL);

    if (flags < 0 || ::fcntl(m_descriptor, F_SETFL, flags | O_NONBLOCK) < 0) {
        const int errorNumber = errno;
        terminate();
        errno = errorNumber;
        return PosixPtyBackendHelper::systemFailure(QStringLiteral("pty_nonblocking_failed"), QStringLiteral("The pseudo-terminal could not enter nonblocking mode"));
    }

    m_readNotifier = new QSocketNotifier(m_descriptor, QSocketNotifier::Read, this);
    m_writeNotifier = new QSocketNotifier(m_descriptor, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &PosixPtyBackend::drainOutput);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &PosixPtyBackend::flushInput);
    m_exitTimer.start();
    m_lastWorkingDirectory = QDir::cleanPath(workingDirectory);
    m_directoryTimer.start();

    return Result<void>::success();
}

Result<void> PosixPtyBackend::write(const QByteArray& bytes) {
    if (!running()) {
        return Result<void>::failure({"terminal_not_running", "The terminal process is not running", {}});
    }
    if (bytes.size() > maximumInputQueueSize - m_pendingInput.size()) {
        return Result<void>::failure({"terminal_input_queue_full", "The terminal input queue is full", {}});
    }

    m_pendingInput.append(bytes);
    flushInput();
    return Result<void>::success();
}

Result<void> PosixPtyBackend::resize(int columns, int rows, int cellWidth, int cellHeight) {
    if (!running()) {
        return Result<void>::failure({"terminal_not_running", "The terminal process is not running", {}});
    }
    if (!TerminalDimensions::validTerminalGrid(columns, rows) || !TerminalDimensions::validTerminalCellSize(cellWidth, cellHeight)) {
        return Result<void>::failure({"terminal_size_invalid", "The terminal dimensions are invalid", QStringLiteral("%1x%2 at %3x%4").arg(columns).arg(rows).arg(cellWidth).arg(cellHeight)});
    }

    winsize size{};
    size.ws_col = static_cast<unsigned short>(columns);
    size.ws_row = static_cast<unsigned short>(rows);
    size.ws_xpixel = static_cast<unsigned short>(std::clamp(static_cast<qint64>(columns) * cellWidth, qint64{0}, qint64{65535}));
    size.ws_ypixel = static_cast<unsigned short>(std::clamp(static_cast<qint64>(rows) * cellHeight, qint64{0}, qint64{65535}));

    if (::ioctl(m_descriptor, TIOCSWINSZ, &size) != 0) {
        return PosixPtyBackendHelper::systemFailure(QStringLiteral("terminal_resize_failed"), QStringLiteral("The terminal could not be resized"));
    }

    return Result<void>::success();
}

void PosixPtyBackend::setOutputPaused(bool paused) {
    if (m_readNotifier) {
        m_readNotifier->setEnabled(!paused);
    }
}

void PosixPtyBackend::terminate() {
    m_exitTimer.stop();
    m_directoryTimer.stop();
    releaseNotifiers();
    m_pendingInput.clear();
    m_lastWorkingDirectory.clear();

    const pid_t childProcessId = static_cast<pid_t>(m_childProcessId);
    m_childProcessId = -1;

    if (childProcessId > 0) {
        ::kill(-childProcessId, SIGHUP);
        ::kill(childProcessId, SIGHUP);
        ChildProcessReaper::instance().schedule(childProcessId);
    }

    closeDescriptor();
}

bool PosixPtyBackend::running() const {
    return m_childProcessId > 0 && m_descriptor >= 0;
}

void PosixPtyBackend::drainOutput() {
    std::array<char, 64 * 1024> buffer{};
    QByteArray batch;
    bool ended = false;

    for (;;) {
        const ssize_t count = ::read(m_descriptor, buffer.data(), buffer.size());
        if (count > 0) {
            batch.append(buffer.data(), static_cast<qsizetype>(count));
            if (batch.size() >= maximumOutputBatchSize) {
                break;
            }
            continue;
        }
        if (count == 0 || (count < 0 && errno == EIO)) {
            ended = true;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            emit backendError(PosixPtyBackendHelper::errorText(errno));
        }
        break;
    }

    if (!batch.isEmpty()) {
        emit outputReady(batch);
    }

    // The last thing a program wrote reaches the reader before the reader is told that program ended.
    if (ended) {
        checkProcess();
    }
}

void PosixPtyBackend::flushInput() {
    while (!m_pendingInput.isEmpty()) {
        const ssize_t count = ::write(m_descriptor, m_pendingInput.constData(), static_cast<std::size_t>(m_pendingInput.size()));
        if (count > 0) {
            m_pendingInput.remove(0, static_cast<qsizetype>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            emit backendError(PosixPtyBackendHelper::errorText(errno));
            m_pendingInput.clear();
        }
        break;
    }

    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(!m_pendingInput.isEmpty());
    }
}

void PosixPtyBackend::checkProcess() {
    if (m_childProcessId <= 0) {
        return;
    }

    int status = 0;
    const pid_t result = ::waitpid(static_cast<pid_t>(m_childProcessId), &status, WNOHANG);

    if (result > 0) {
        finishProcess(status);
    }
}

void PosixPtyBackend::refreshWorkingDirectory() {
    const pid_t foregroundProcess = ::tcgetpgrp(m_descriptor);

    if (foregroundProcess <= 0) {
        return;
    }

    QString directory = PosixPtyBackendHelper::processWorkingDirectory(foregroundProcess);

    if (directory.isEmpty()) {
        return;
    }

    directory = QDir::cleanPath(directory);

    if (directory == m_lastWorkingDirectory) {
        return;
    }

    m_lastWorkingDirectory = directory;
    emit workingDirectoryChanged(directory);
}

void PosixPtyBackend::closeDescriptor() {
    if (m_descriptor >= 0) {
        ::close(m_descriptor);
        m_descriptor = -1;
    }
}

// A notifier is reached from the read it is delivering, so it stops watching at once and is destroyed once the event loop returns to it.
void PosixPtyBackend::releaseNotifier(QSocketNotifier*& notifier) {
    if (notifier == nullptr) {
        return;
    }

    notifier->setEnabled(false);
    notifier->disconnect(this);
    notifier->deleteLater();
    notifier = nullptr;
}

void PosixPtyBackend::releaseNotifiers() {
    releaseNotifier(m_readNotifier);
    releaseNotifier(m_writeNotifier);
}

void PosixPtyBackend::finishProcess(int status) {
    m_exitTimer.stop();
    m_directoryTimer.stop();
    releaseNotifiers();
    m_pendingInput.clear();
    closeDescriptor();
    m_childProcessId = -1;

    int exitCode = -1;

    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        exitCode = 128 + WTERMSIG(status);
    }

    emit processExited(exitCode);
}

} // namespace workpane::terminalcore
