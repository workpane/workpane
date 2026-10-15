#include "support/WindowsFileAccessHelper.h"

#include "platform/windows/WindowsTextHelper.h"

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>

#include <memory>
#include <string>

namespace workpane::tests {

bool WindowsFileAccessHelper::privateDirectory(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());

    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }

    auto name = path.wstring();
    PSECURITY_DESCRIPTOR received = nullptr;
    PSID owner = nullptr;
    PACL permissions = nullptr;
    const DWORD inspected = GetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &permissions, nullptr, &received);
    const std::unique_ptr<void, decltype(&LocalFree)> descriptor(received, LocalFree);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;

    if (inspected != ERROR_SUCCESS || owner == nullptr || permissions == nullptr || permissions->AceCount != 1 || !GetSecurityDescriptorControl(descriptor.get(), &control, &revision) || (control & SE_DACL_PROTECTED) == 0) {
        return false;
    }

    void* entry = nullptr;

    if (!GetAce(permissions, 0, &entry) || entry == nullptr) {
        return false;
    }

    const auto* header = static_cast<const ACE_HEADER*>(entry);

    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || header->AceSize < sizeof(ACCESS_ALLOWED_ACE)) {
        return false;
    }

    auto* access = static_cast<ACCESS_ALLOWED_ACE*>(entry);

    return access->Header.AceFlags == (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) && access->Mask == FILE_ALL_ACCESS && EqualSid(owner, &access->SidStart) != 0;
}

Result<void> WindowsFileAccessHelper::protect(const std::filesystem::path& path) {
    auto name = path.wstring();
    PSECURITY_DESCRIPTOR received = nullptr;
    PACL access = nullptr;
    const DWORD inspected = GetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &access, nullptr, &received);
    const std::unique_ptr<void, decltype(&LocalFree)> descriptor(received, LocalFree);

    if (inspected != ERROR_SUCCESS) {
        return Result<void>::failure({"test_file_access_failed", "The test file permissions could not be read", std::to_string(inspected)});
    }

    const DWORD changed = SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, access, nullptr);

    if (changed != ERROR_SUCCESS) {
        return Result<void>::failure({"test_file_access_failed", "The test file permissions could not be protected", std::to_string(changed)});
    }

    return Result<void>::success();
}

Result<std::string> WindowsFileAccessHelper::snapshot(const std::filesystem::path& path) {
    auto name = path.wstring();
    PSECURITY_DESCRIPTOR received = nullptr;
    const DWORD inspected = GetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr, &received);
    const std::unique_ptr<void, decltype(&LocalFree)> descriptor(received, LocalFree);

    if (inspected != ERROR_SUCCESS) {
        return Result<std::string>::failure({"test_file_access_failed", "The test file permissions could not be read", std::to_string(inspected)});
    }

    const auto automatic = static_cast<SECURITY_DESCRIPTOR_CONTROL>(SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED);

    if (SetSecurityDescriptorControl(received, automatic, 0) == 0) {
        return Result<std::string>::failure({"test_file_access_failed", "The test file inheritance state could not be normalized", std::to_string(GetLastError())});
    }

    LPWSTR text = nullptr;

    if (ConvertSecurityDescriptorToStringSecurityDescriptorW(received, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &text, nullptr) == 0) {
        return Result<std::string>::failure({"test_file_access_failed", "The test file permissions could not be described", std::to_string(GetLastError())});
    }

    const std::unique_ptr<void, decltype(&LocalFree)> described(text, LocalFree);

    return Result<std::string>::success(platform::WindowsTextHelper::narrow(text));
}

} // namespace workpane::tests
