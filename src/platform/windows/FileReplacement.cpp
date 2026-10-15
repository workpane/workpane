#include "platform/FileReplacement.h"

#include <windows.h>

#include <aclapi.h>

#include <memory>
#include <thread>
#include <tuple>

namespace workpane::platform {

struct FileReplacement::Creation final {
    static void release(void* value) {
        std::ignore = DestroyPrivateObjectSecurity(&value);
    }

    std::unique_ptr<void, decltype(&CloseHandle)> m_file{nullptr, CloseHandle};
    std::unique_ptr<void, decltype(&release)> m_permissions{nullptr, release};
    bool m_published{false};
    bool m_completed{false};

    ~Creation() {
        if (m_published && !m_completed) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            std::ignore = SetFileInformationByHandle(m_file.get(), FileDispositionInfo, &disposition, static_cast<DWORD>(sizeof(disposition)));
        }
    }

    [[nodiscard]] std::error_code prepare(const std::filesystem::path& source, const std::filesystem::path& parent) {
        const auto opened = CreateFileW(source.c_str(), DELETE | READ_CONTROL | WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

        if (opened == INVALID_HANDLE_VALUE) {
            return {static_cast<int>(GetLastError()), std::system_category()};
        }

        m_file.reset(opened);
        auto path = parent.wstring();
        PSECURITY_DESCRIPTOR received = nullptr;
        const auto read = GetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr, &received);

        if (read != ERROR_SUCCESS) {
            return {static_cast<int>(read), std::system_category()};
        }

        const std::unique_ptr<void, decltype(&LocalFree)> descriptor(received, LocalFree);
        HANDLE openedToken = nullptr;

        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &openedToken) == 0) {
            return {static_cast<int>(GetLastError()), std::system_category()};
        }

        const std::unique_ptr<void, decltype(&CloseHandle)> token(openedToken, CloseHandle);
        GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
        PSECURITY_DESCRIPTOR created = nullptr;

        if (CreatePrivateObjectSecurityEx(descriptor.get(), nullptr, &created, nullptr, FALSE, SEF_DACL_AUTO_INHERIT, token.get(), &mapping) == 0) {
            return {static_cast<int>(GetLastError()), std::system_category()};
        }

        m_permissions.reset(created);

        return {};
    }

    [[nodiscard]] std::error_code inherit() {
        PACL access = nullptr;
        BOOL present = FALSE;
        BOOL defaulted = FALSE;

        if (GetSecurityDescriptorDacl(m_permissions.get(), &present, &access, &defaulted) == 0) {
            return {static_cast<int>(GetLastError()), std::system_category()};
        }

        const auto applied = SetSecurityInfo(m_file.get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, present ? access : nullptr, nullptr);

        if (applied != ERROR_SUCCESS) {
            return {static_cast<int>(applied), std::system_category()};
        }

        m_completed = true;

        return {};
    }

    void publish() {
        m_published = true;
    }
};

// A program that holds a file for a moment, such as a scanner reading what was just written, refuses the move until it lets go, so a refused move is tried again within a bound.
std::error_code FileReplacement::replace(const std::filesystem::path& from, const std::filesystem::path& to) {
    const DWORD attributes = GetFileAttributesW(to.c_str());

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD failure = GetLastError();

        if (failure != ERROR_FILE_NOT_FOUND) {
            return {static_cast<int>(failure), std::system_category()};
        }

        return create(from, to);
    }

    const auto deadline = std::chrono::steady_clock::now() + busyBound;

    while (ReplaceFileW(to.c_str(), from.c_str(), nullptr, 0, nullptr, nullptr) == 0) {
        const DWORD failure = GetLastError();
        const bool held = failure == ERROR_SHARING_VIOLATION || failure == ERROR_LOCK_VIOLATION || failure == ERROR_ACCESS_DENIED;

        if (!held || std::chrono::steady_clock::now() >= deadline) {
            return {static_cast<int>(failure), std::system_category()};
        }

        std::this_thread::sleep_for(retryPause);
    }

    return {};
}

std::error_code FileReplacement::create(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    const auto destination = std::filesystem::absolute(to, error);

    if (error) {
        return error;
    }

    Creation creation;

    if (const auto prepared = creation.prepare(from, destination.parent_path()); prepared) {
        return prepared;
    }

    if (MoveFileExW(from.c_str(), to.c_str(), 0) == 0) {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }

    creation.publish();

    // Inheritance uses the file's current parent, so private staging access is released only after exclusive publication.
    return creation.inherit();
}

} // namespace workpane::platform
