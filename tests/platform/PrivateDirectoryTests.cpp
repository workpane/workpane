#include "platform/FileReplacement.h"
#include "platform/PrivateDirectoryHelper.h"
#include "support/TemporaryDirectory.h"

#include <gtest/gtest.h>

#if defined(_WIN32)
#include <windows.h>

#include <aclapi.h>
#endif

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace workpane::platform {

TEST(PrivateDirectoryHelper, CreatesAPrivateFolderWithoutTakingAnExistingOne) {
    const tests::TemporaryDirectory data;
    const auto directory = data.path() / "private";
    ASSERT_FALSE(PrivateDirectoryHelper::create(directory));
    ASSERT_TRUE(std::filesystem::is_directory(directory));
    EXPECT_TRUE(PrivateDirectoryHelper::restricted(directory));
    EXPECT_FALSE(PrivateDirectoryHelper::restricted(data.path() / "missing"));
    std::ofstream(directory / "value") << "retained";
    EXPECT_TRUE(PrivateDirectoryHelper::create(directory));
    EXPECT_TRUE(std::filesystem::exists(directory / "value"));

#if defined(_WIN32)
    auto path = directory.wstring();
    PSECURITY_DESCRIPTOR received = nullptr;
    PACL permissions = nullptr;
    ASSERT_EQ(GetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &permissions, nullptr, &received), ERROR_SUCCESS);
    const std::unique_ptr<void, decltype(&LocalFree)> descriptor(received, LocalFree);
    ASSERT_NE(permissions, nullptr);
    EXPECT_EQ(permissions->AceCount, 1);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ASSERT_TRUE(GetSecurityDescriptorControl(descriptor.get(), &control, &revision));
    EXPECT_NE(control & SE_DACL_PROTECTED, 0);
#else
    EXPECT_EQ(std::filesystem::status(directory).permissions(), std::filesystem::perms::owner_all);
    std::filesystem::permissions(directory, std::filesystem::perms::group_read, std::filesystem::perm_options::add);
    EXPECT_FALSE(PrivateDirectoryHelper::restricted(directory));
#endif
}

TEST(FileReplacement, KeepsPrivateAccessAndExecutablePermissionsWhenReplacingAFile) {
    const tests::TemporaryDirectory data;
    const auto directory = data.path() / "private";
    const auto source = data.path() / "source";
    const auto destination = directory / "document";
    ASSERT_FALSE(PrivateDirectoryHelper::create(directory));
    std::ofstream(source, std::ios::binary) << "new";
    std::ofstream(destination, std::ios::binary) << "original";

#if defined(_WIN32)
    auto path = destination.wstring();
    PSECURITY_DESCRIPTOR inherited = nullptr;
    PACL inheritedAccess = nullptr;
    ASSERT_EQ(GetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &inheritedAccess, nullptr, &inherited), ERROR_SUCCESS);
    const std::unique_ptr<void, decltype(&LocalFree)> inheritedDescriptor(inherited, LocalFree);
    ASSERT_NE(inheritedAccess, nullptr);
    ASSERT_EQ(SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, inheritedAccess, nullptr), ERROR_SUCCESS);
    PSECURITY_DESCRIPTOR original = nullptr;
    PACL originalAccess = nullptr;
    ASSERT_EQ(GetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &originalAccess, nullptr, &original), ERROR_SUCCESS);
    const std::unique_ptr<void, decltype(&LocalFree)> originalDescriptor(original, LocalFree);
    ASSERT_NE(originalAccess, nullptr);
#else
    const auto permissions = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec;
    std::filesystem::permissions(destination, permissions);
#endif

    ASSERT_FALSE(FileReplacement::replace(source, destination));
    EXPECT_FALSE(std::filesystem::exists(source));
    std::ifstream saved(destination, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()), "new");

#if defined(_WIN32)
    PSECURITY_DESCRIPTOR replaced = nullptr;
    PACL replacedAccess = nullptr;
    ASSERT_EQ(GetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &replacedAccess, nullptr, &replaced), ERROR_SUCCESS);
    const std::unique_ptr<void, decltype(&LocalFree)> replacedDescriptor(replaced, LocalFree);
    ASSERT_NE(replacedAccess, nullptr);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ASSERT_TRUE(GetSecurityDescriptorControl(replacedDescriptor.get(), &control, &revision));
    EXPECT_NE(control & SE_DACL_PROTECTED, 0);
    ASSERT_EQ(replacedAccess->AceCount, originalAccess->AceCount);

    for (DWORD index = 0; index < originalAccess->AceCount; ++index) {
        void* originalEntry = nullptr;
        void* replacedEntry = nullptr;
        ASSERT_TRUE(GetAce(originalAccess, index, &originalEntry));
        ASSERT_TRUE(GetAce(replacedAccess, index, &replacedEntry));
        const auto* originalHeader = static_cast<const ACE_HEADER*>(originalEntry);
        const auto* replacedHeader = static_cast<const ACE_HEADER*>(replacedEntry);
        ASSERT_EQ(originalHeader->AceType, ACCESS_ALLOWED_ACE_TYPE);
        ASSERT_EQ(replacedHeader->AceType, originalHeader->AceType);
        EXPECT_EQ(replacedHeader->AceFlags, originalHeader->AceFlags);
        auto* originalRule = static_cast<ACCESS_ALLOWED_ACE*>(originalEntry);
        auto* replacedRule = static_cast<ACCESS_ALLOWED_ACE*>(replacedEntry);
        EXPECT_EQ(replacedRule->Mask, originalRule->Mask);
        EXPECT_TRUE(EqualSid(&originalRule->SidStart, &replacedRule->SidStart));
    }
#else
    EXPECT_EQ(std::filesystem::status(destination).permissions(), permissions);
#endif
}

TEST(FileReplacement, GivesANewFileTheDestinationAccessInsteadOfItsPrivateStagingAccess) {
    const tests::TemporaryDirectory data(std::filesystem::current_path());
    const auto staging = data.path() / "private";
    const auto source = staging / "content";
    const auto destination = data.path() / "published";
    const auto control = data.path() / "control";
    ASSERT_FALSE(PrivateDirectoryHelper::create(staging));
    std::ofstream(source, std::ios::binary) << "complete";
    std::ofstream(control, std::ios::binary) << "ordinary creation";
    const auto created = FileReplacement::create(source, std::filesystem::relative(destination));
    ASSERT_FALSE(created) << created.message() << " (" << created.value() << ")";
    EXPECT_FALSE(std::filesystem::exists(source));
    std::ifstream saved(destination, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()), "complete");

#if defined(_WIN32)
    auto controlPath = control.wstring();
    auto destinationPath = destination.wstring();
    PSECURITY_DESCRIPTOR expected = nullptr;
    PSECURITY_DESCRIPTOR received = nullptr;
    PACL expectedAccess = nullptr;
    PACL receivedAccess = nullptr;
    ASSERT_EQ(GetNamedSecurityInfoW(controlPath.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &expectedAccess, nullptr, &expected), ERROR_SUCCESS);
    const std::unique_ptr<void, decltype(&LocalFree)> expectedDescriptor(expected, LocalFree);
    ASSERT_EQ(GetNamedSecurityInfoW(destinationPath.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &receivedAccess, nullptr, &received), ERROR_SUCCESS);
    const std::unique_ptr<void, decltype(&LocalFree)> receivedDescriptor(received, LocalFree);
    ASSERT_NE(expectedAccess, nullptr);
    ASSERT_NE(receivedAccess, nullptr);
    SECURITY_DESCRIPTOR_CONTROL expectedControl = 0;
    SECURITY_DESCRIPTOR_CONTROL receivedControl = 0;
    DWORD revision = 0;
    ASSERT_TRUE(GetSecurityDescriptorControl(expectedDescriptor.get(), &expectedControl, &revision));
    ASSERT_TRUE(GetSecurityDescriptorControl(receivedDescriptor.get(), &receivedControl, &revision));
    EXPECT_EQ(receivedControl & SE_DACL_PROTECTED, expectedControl & SE_DACL_PROTECTED);
    EXPECT_EQ(receivedAccess->AclRevision, expectedAccess->AclRevision);
    ASSERT_EQ(receivedAccess->AceCount, expectedAccess->AceCount);

    for (DWORD index = 0; index < expectedAccess->AceCount; ++index) {
        void* expectedEntry = nullptr;
        void* receivedEntry = nullptr;
        ASSERT_TRUE(GetAce(expectedAccess, index, &expectedEntry));
        ASSERT_TRUE(GetAce(receivedAccess, index, &receivedEntry));
        const auto* expectedHeader = static_cast<const ACE_HEADER*>(expectedEntry);
        const auto* receivedHeader = static_cast<const ACE_HEADER*>(receivedEntry);
        ASSERT_EQ(receivedHeader->AceSize, expectedHeader->AceSize);
        EXPECT_EQ(std::memcmp(receivedEntry, expectedEntry, expectedHeader->AceSize), 0);
    }
#else
    EXPECT_EQ(std::filesystem::status(destination).permissions(), std::filesystem::status(control).permissions());
#endif
}

} // namespace workpane::platform
