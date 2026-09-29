#include "persistence/InstanceLock.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace workpane::persistence {

Result<InstanceLock> InstanceLock::acquire(const std::filesystem::path& dataDirectory) {
    const std::filesystem::path file = dataDirectory / lockName;

#if defined(_WIN32)
    // Other programs may open the file, as a scanner does right after a write, so only the exclusive lock of its bytes tells another instance apart.
    HANDLE handle = CreateFileW(file.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        return Result<InstanceLock>::failure({"instance_lock_failed", "The instance lock could not be created", file.string() + ": " + std::to_string(GetLastError())});
    }

    OVERLAPPED whole{};

    if (LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &whole) == FALSE) {
        const DWORD failure = GetLastError();
        CloseHandle(handle);

        if (failure == ERROR_LOCK_VIOLATION) {
            return Result<InstanceLock>::failure({"instance_already_running", "Another instance already uses this data directory", file.string()});
        }

        return Result<InstanceLock>::failure({"instance_lock_failed", "The instance lock could not be taken", file.string() + ": " + std::to_string(failure)});
    }

    // The mark the previous instance left is read before this one writes its own.
    std::array<char, 16> previous{};
    DWORD read = 0;
    DWORD written = 0;
    const bool readable = ReadFile(handle, previous.data(), static_cast<DWORD>(previous.size()), &read, nullptr) != FALSE;
    const bool marked = SetFilePointer(handle, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER && SetEndOfFile(handle) != FALSE && WriteFile(handle, runningMark.data(), static_cast<DWORD>(runningMark.size()), &written, nullptr) != FALSE && written == runningMark.size();

    if (!readable || !marked) {
        CloseHandle(handle);
        return Result<InstanceLock>::failure({"instance_lock_failed", "The instance lock could not be written", file.string()});
    }

    return Result<InstanceLock>::success(InstanceLock(reinterpret_cast<std::intptr_t>(handle), std::string_view(previous.data(), read) == runningMark));
#else
    const int descriptor = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);

    if (descriptor < 0) {
        return Result<InstanceLock>::failure({"instance_lock_failed", "The instance lock could not be created", file.string()});
    }

    // Only a lock another process holds means another instance, and any other failure of the lock is named by what the system reported.
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        const int failure = errno;
        ::close(descriptor);

        if (failure == EWOULDBLOCK) {
            return Result<InstanceLock>::failure({"instance_already_running", "Another instance already uses this data directory", file.string()});
        }

        return Result<InstanceLock>::failure({"instance_lock_failed", "The instance lock could not be taken", file.string() + ": " + std::strerror(failure)});
    }

    // The mark the previous instance left is read before this one writes its own.
    std::array<char, 16> previous{};
    const ssize_t read = ::pread(descriptor, previous.data(), previous.size(), 0);
    const bool marked = ::ftruncate(descriptor, 0) == 0 && ::pwrite(descriptor, runningMark.data(), runningMark.size(), 0) == static_cast<ssize_t>(runningMark.size());

    if (read < 0 || !marked) {
        ::flock(descriptor, LOCK_UN);
        ::close(descriptor);
        return Result<InstanceLock>::failure({"instance_lock_failed", "The instance lock could not be written", file.string()});
    }

    return Result<InstanceLock>::success(InstanceLock(descriptor, std::string_view(previous.data(), static_cast<std::size_t>(read)) == runningMark));
#endif
}

InstanceLock::InstanceLock(std::intptr_t handle, bool recovered) : m_handle(handle), m_recovered(recovered) {}

InstanceLock::InstanceLock(InstanceLock&& other) noexcept : m_handle(std::exchange(other.m_handle, -1)), m_recovered(other.m_recovered) {}

InstanceLock& InstanceLock::operator=(InstanceLock&& other) noexcept {
    if (this != &other) {
        release();
        m_handle = std::exchange(other.m_handle, -1);
        m_recovered = other.m_recovered;
    }

    return *this;
}

bool InstanceLock::recovered() const {
    return m_recovered;
}

InstanceLock::~InstanceLock() {
    release();
}

void InstanceLock::release() {
    if (m_handle == -1) {
        return;
    }

    // Emptying the file before letting it go is what tells the next start that this one stopped cleanly.
#if defined(_WIN32)
    const auto handle = reinterpret_cast<HANDLE>(m_handle);
    SetFilePointer(handle, 0, nullptr, FILE_BEGIN);
    SetEndOfFile(handle);
    CloseHandle(handle);
#else
    std::ignore = ::ftruncate(static_cast<int>(m_handle), 0);
    ::flock(static_cast<int>(m_handle), LOCK_UN);
    ::close(static_cast<int>(m_handle));
#endif

    m_handle = -1;
}

} // namespace workpane::persistence
