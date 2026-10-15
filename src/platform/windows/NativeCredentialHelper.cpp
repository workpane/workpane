#include "platform/NativeCredentialHelper.h"

#include "platform/windows/WindowsTextHelper.h"

#include <windows.h>

#include <wincred.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace workpane::platform {

Result<std::optional<std::string>> NativeCredentialHelper::read(std::string_view service, std::string_view account) {
    const std::wstring target = WindowsTextHelper::wide(std::string(service) + "/" + std::string(account));
    PCREDENTIALW value = nullptr;

    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &value)) {
        const DWORD status = GetLastError();

        if (status == ERROR_NOT_FOUND) {
            return Result<std::optional<std::string>>::success(std::nullopt);
        }

        return Result<std::optional<std::string>>::failure({"credential_store_unavailable", "The system credential store could not read the credential", std::to_string(status)});
    }

    const auto size = static_cast<std::size_t>(value->CredentialBlobSize);

    if (size > maximumValueBytes) {
        CredFree(value);
        return Result<std::optional<std::string>>::failure({"credential_value_invalid", "The stored credential is invalid", ""});
    }

    std::string secret = size == 0 ? std::string() : std::string(reinterpret_cast<const char*>(value->CredentialBlob), size);
    CredFree(value);

    return Result<std::optional<std::string>>::success(std::move(secret));
}

Result<void> NativeCredentialHelper::write(std::string_view service, std::string_view account, std::string_view value) {
    if (const auto valid = validateValue(value); !valid.hasValue()) {
        return valid;
    }

    std::wstring target = WindowsTextHelper::wide(std::string(service) + "/" + std::string(account));
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(value.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;

    if (!CredWriteW(&credential, 0)) {
        return Result<void>::failure({"credential_store_unavailable", "The system credential store could not save the credential", std::to_string(GetLastError())});
    }

    return Result<void>::success();
}

Result<void> NativeCredentialHelper::erase(std::string_view service, std::string_view account) {
    const std::wstring target = WindowsTextHelper::wide(std::string(service) + "/" + std::string(account));

    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
        const DWORD status = GetLastError();

        if (status != ERROR_NOT_FOUND) {
            return Result<void>::failure({"credential_store_unavailable", "The system credential store could not remove the credential", std::to_string(status)});
        }
    }

    return Result<void>::success();
}

} // namespace workpane::platform
