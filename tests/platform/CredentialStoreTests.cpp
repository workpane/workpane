#include "platform/NativeSystemServices.h"
#include "support/TemporaryDirectory.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace workpane::tests {

TEST(CredentialStoreTest, StoresAvailablePasswordsAndReportsUnavailableNativeStorage) {
    TemporaryDirectory identity;
    platform::NativeSystemServices system;
    const std::string service = "Workpane.test." + identity.path().filename().string();
    const std::string account = "native-password";
    const auto missing = system.readCredential(service, account);

#if defined(__linux__)
    if (!missing.hasValue()) {
        EXPECT_EQ(missing.error().code, "credential_store_unavailable");
        EXPECT_FALSE(system.writeCredential(service, account, "Private test credential").hasValue());
        EXPECT_FALSE(system.eraseCredential(service, account).hasValue());
        return;
    }
#endif

    ASSERT_TRUE(missing.hasValue());
    EXPECT_FALSE(missing.value().has_value());
    const auto created = system.writeCredential(service, account, "Private test credential");
#if defined(__linux__)
    if (!created.hasValue()) {
        EXPECT_TRUE(created.error().code == "credential_store_unavailable" || created.error().code == "credential_store_locked");
        const auto absent = system.readCredential(service, account);
        ASSERT_TRUE(absent.hasValue());
        EXPECT_FALSE(absent.value().has_value());
        EXPECT_TRUE(system.eraseCredential(service, account).hasValue());
        return;
    }
#endif

    ASSERT_TRUE(created.hasValue()) << created.error().code;
    const auto first = system.readCredential(service, account);
    EXPECT_TRUE(first.hasValue());

    if (first.hasValue()) {
        EXPECT_EQ(first.value(), std::optional<std::string>("Private test credential"));
    }

    const auto replaced = system.writeCredential(service, account, "Private updated credential");
    EXPECT_TRUE(replaced.hasValue());
    const auto second = system.readCredential(service, account);
    EXPECT_TRUE(second.hasValue());

    if (second.hasValue()) {
        EXPECT_EQ(second.value(), std::optional<std::string>("Private updated credential"));
    }

    const auto invalid = system.writeCredential(service, account, "");
    ASSERT_FALSE(invalid.hasValue());
    EXPECT_EQ(invalid.error().code, "credential_request_invalid");
    const auto empty = system.readCredential(service, account);
    EXPECT_TRUE(empty.hasValue());

    if (empty.hasValue()) {
        EXPECT_EQ(empty.value(), std::optional<std::string>("Private updated credential"));
    }

    EXPECT_TRUE(system.eraseCredential(service, account).hasValue());
    EXPECT_TRUE(system.eraseCredential(service, account).hasValue());
    const auto removed = system.readCredential(service, account);
    ASSERT_TRUE(removed.hasValue());
    EXPECT_FALSE(removed.value().has_value());
}

} // namespace workpane::tests
