#include "platform/NativeCredentialHelper.h"

#include "platform/linux/LinuxSecretService.h"

#include <optional>
#include <string>
#include <string_view>

namespace workpane::platform {

Result<std::optional<std::string>> NativeCredentialHelper::read(std::string_view service, std::string_view account) {
    auto store = LinuxSecretService::open();

    if (!store.hasValue()) {
        return Result<std::optional<std::string>>::failure(store.error());
    }

    return store.value()->read(service, account);
}

Result<void> NativeCredentialHelper::write(std::string_view service, std::string_view account, std::string_view value) {
    if (const auto valid = validateValue(value); !valid.hasValue()) {
        return valid;
    }

    auto store = LinuxSecretService::open();

    if (!store.hasValue()) {
        return Result<void>::failure(store.error());
    }

    return store.value()->write(service, account, value);
}

Result<void> NativeCredentialHelper::erase(std::string_view service, std::string_view account) {
    auto store = LinuxSecretService::open();

    if (!store.hasValue()) {
        return Result<void>::failure(store.error());
    }

    return store.value()->erase(service, account);
}

} // namespace workpane::platform
