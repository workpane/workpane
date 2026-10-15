#include "platform/NativeCredentialHelper.h"

#include "platform/macos/MacTextHelper.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace workpane::platform {

Result<std::optional<std::string>> NativeCredentialHelper::read(std::string_view service, std::string_view account) {
    @autoreleasepool {
        NSDictionary* query = @{(id)kSecClass : (id)kSecClassGenericPassword, (id)kSecAttrService : MacTextHelper::string(service), (id)kSecAttrAccount : MacTextHelper::string(account), (id)kSecMatchLimit : (id)kSecMatchLimitOne, (id)kSecReturnData : @YES};
        CFTypeRef value = nullptr;
        const OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &value);

        if (status == errSecItemNotFound) {
            return Result<std::optional<std::string>>::success(std::nullopt);
        }

        if (status != errSecSuccess) {
            return Result<std::optional<std::string>>::failure({"credential_store_unavailable", "The system credential store could not read the credential", std::to_string(status)});
        }

        const auto data = static_cast<CFDataRef>(value);
        const auto size = static_cast<std::size_t>(CFDataGetLength(data));

        if (size > maximumValueBytes) {
            CFRelease(value);
            return Result<std::optional<std::string>>::failure({"credential_value_invalid", "The stored credential is invalid", ""});
        }

        std::string secret = size == 0 ? std::string() : std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), size);
        CFRelease(value);

        return Result<std::optional<std::string>>::success(std::move(secret));
    }
}

Result<void> NativeCredentialHelper::write(std::string_view service, std::string_view account, std::string_view value) {
    if (const auto valid = validateValue(value); !valid.hasValue()) {
        return valid;
    }

    @autoreleasepool {
        NSDictionary* query = @{(id)kSecClass : (id)kSecClassGenericPassword, (id)kSecAttrService : MacTextHelper::string(service), (id)kSecAttrAccount : MacTextHelper::string(account)};
        NSData* data = [NSData dataWithBytes:value.data() length:value.size()];
        OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef) @{(id)kSecValueData : data});

        if (status == errSecItemNotFound) {
            NSMutableDictionary* item = [query mutableCopy];
            item[(id)kSecValueData] = data;
            item[(id)kSecAttrLabel] = MacTextHelper::string(service);
            status = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
        }

        if (status != errSecSuccess) {
            return Result<void>::failure({"credential_store_unavailable", "The system credential store could not save the credential", std::to_string(status)});
        }
    }

    return Result<void>::success();
}

Result<void> NativeCredentialHelper::erase(std::string_view service, std::string_view account) {
    @autoreleasepool {
        NSDictionary* query = @{(id)kSecClass : (id)kSecClassGenericPassword, (id)kSecAttrService : MacTextHelper::string(service), (id)kSecAttrAccount : MacTextHelper::string(account)};
        const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);

        if (status != errSecSuccess && status != errSecItemNotFound) {
            return Result<void>::failure({"credential_store_unavailable", "The system credential store could not remove the credential", std::to_string(status)});
        }
    }

    return Result<void>::success();
}

} // namespace workpane::platform
