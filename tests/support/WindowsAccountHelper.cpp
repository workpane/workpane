#include "support/WindowsAccountHelper.h"

#include "platform/windows/WindowsTextHelper.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace workpane::tests {

struct WindowsAccountHelper::Identity final {
    TOKEN_USER user;
    std::array<std::byte, SECURITY_MAX_SID_SIZE> storage;
};

Result<std::string> WindowsAccountHelper::current() {
    HANDLE opened = nullptr;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &opened) == 0) {
        return Result<std::string>::failure({"account_unavailable", "The current account could not be read", ""});
    }

    const std::unique_ptr<void, decltype(&CloseHandle)> token(opened, CloseHandle);
    Identity identity{};
    DWORD size = 0;

    if (GetTokenInformation(token.get(), TokenUser, &identity, static_cast<DWORD>(sizeof(identity)), &size) == 0) {
        return Result<std::string>::failure({"account_unavailable", "The current account could not be read", ""});
    }

    std::array<wchar_t, 1025> name{};
    std::array<wchar_t, 1025> domain{};
    DWORD nameSize = static_cast<DWORD>(name.size());
    DWORD domainSize = static_cast<DWORD>(domain.size());
    SID_NAME_USE kind{};

    if (LookupAccountSidW(nullptr, identity.user.User.Sid, name.data(), &nameSize, domain.data(), &domainSize, &kind) == 0 || name[0] == L'\0') {
        return Result<std::string>::failure({"account_unavailable", "The current account could not be read", ""});
    }

    const auto account = domain[0] == L'\0' ? std::wstring(name.data()) : std::wstring(domain.data()) + L"\\" + name.data();

    return Result<std::string>::success(platform::WindowsTextHelper::narrow(account.c_str()));
}

} // namespace workpane::tests
