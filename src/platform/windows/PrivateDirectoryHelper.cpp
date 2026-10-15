#include "platform/PrivateDirectoryHelper.h"

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace workpane::platform {

struct PrivateDirectoryHelper::Identity final {
    TOKEN_USER user;
    std::array<std::byte, SECURITY_MAX_SID_SIZE> storage;
};

std::error_code PrivateDirectoryHelper::create(const std::filesystem::path& path) {
    HANDLE opened = nullptr;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &opened) == 0) {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }

    const std::unique_ptr<void, decltype(&CloseHandle)> token(opened, CloseHandle);
    Identity identity{};
    DWORD length = 0;

    if (GetTokenInformation(token.get(), TokenUser, &identity, static_cast<DWORD>(sizeof(identity)), &length) == 0) {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }

    LPWSTR encoded = nullptr;

    if (ConvertSidToStringSidW(identity.user.User.Sid, &encoded) == 0) {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }

    const std::unique_ptr<wchar_t, decltype(&LocalFree)> account(encoded, LocalFree);
    const std::wstring rule = std::wstring(L"O:") + account.get() + L"D:P(A;OICI;FA;;;OW)";
    PSECURITY_DESCRIPTOR created = nullptr;

    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(rule.c_str(), SDDL_REVISION_1, &created, nullptr) == 0) {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }

    const std::unique_ptr<void, decltype(&LocalFree)> descriptor(created, LocalFree);
    SECURITY_ATTRIBUTES attributes{static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), descriptor.get(), FALSE};

    if (CreateDirectoryW(path.c_str(), &attributes) == 0) {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }

    return {};
}

bool PrivateDirectoryHelper::restricted(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());

    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }

    HANDLE opened = nullptr;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &opened) == 0) {
        return false;
    }

    const std::unique_ptr<void, decltype(&CloseHandle)> token(opened, CloseHandle);
    Identity identity{};
    DWORD length = 0;

    if (GetTokenInformation(token.get(), TokenUser, &identity, static_cast<DWORD>(sizeof(identity)), &length) == 0) {
        return false;
    }

    PSECURITY_DESCRIPTOR received = nullptr;
    PSID owner = nullptr;
    PACL permissions = nullptr;
    auto name = path.wstring();

    if (GetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &permissions, nullptr, &received) != ERROR_SUCCESS) {
        return false;
    }

    const std::unique_ptr<void, decltype(&LocalFree)> descriptor(received, LocalFree);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;

    if (owner == nullptr || !EqualSid(owner, identity.user.User.Sid) || permissions == nullptr || permissions->AceCount != 1 || !GetSecurityDescriptorControl(descriptor.get(), &control, &revision) || (control & SE_DACL_PROTECTED) == 0) {
        return false;
    }

    void* entry = nullptr;

    if (GetAce(permissions, 0, &entry) == 0 || entry == nullptr) {
        return false;
    }

    const auto* header = static_cast<const ACE_HEADER*>(entry);

    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || header->AceSize < sizeof(ACCESS_ALLOWED_ACE)) {
        return false;
    }

    auto* access = static_cast<ACCESS_ALLOWED_ACE*>(entry);
    alignas(DWORD) std::array<std::byte, SECURITY_MAX_SID_SIZE> ownerRights{};
    DWORD size = static_cast<DWORD>(ownerRights.size());

    return CreateWellKnownSid(WinCreatorOwnerRightsSid, nullptr, ownerRights.data(), &size) != 0 && access->Header.AceFlags == (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) && access->Mask == FILE_ALL_ACCESS && EqualSid(&access->SidStart, ownerRights.data()) != 0;
}

} // namespace workpane::platform
