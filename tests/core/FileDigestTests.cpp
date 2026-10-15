#include "files/FileDigestHelper.h"
#include "support/TemporaryDirectory.h"

#include <gtest/gtest.h>

#include <atomic>
#include <fstream>
#include <string>
#include <string_view>

namespace workpane::tests {

class FileDigestTest : public ::testing::Test {
  protected:
    TemporaryDirectory folder;
    std::atomic<bool> stopping{false};
};

TEST_F(FileDigestTest, HashesEmptyAndKnownBytesAtTheExactSizeBound) {
    const auto path = folder.path() / "content";
    std::ofstream(path, std::ios::binary).close();
    const auto empty = files::FileDigestHelper::sha256(path, 0, stopping);
    ASSERT_TRUE(empty.hasValue());
    EXPECT_EQ(empty.value(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(files::FileDigestHelper::sha256(std::string_view()).value(), empty.value());

    std::ofstream(path, std::ios::binary) << "abc";
    const auto text = files::FileDigestHelper::sha256(path, 3, stopping);
    ASSERT_TRUE(text.hasValue());
    EXPECT_EQ(text.value(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(files::FileDigestHelper::sha256("abc").value(), text.value());

    const std::string binary("\0\xff\r\nabc", 7);
    std::ofstream(path, std::ios::binary).write(binary.data(), 7);
    const auto bytes = files::FileDigestHelper::sha256(path, 7, stopping);
    ASSERT_TRUE(bytes.hasValue());
    EXPECT_EQ(bytes.value(), "6c054d2caf0b5ac869dac381d6995fba75212ea0532f3500bacac9a344378802");
    EXPECT_EQ(files::FileDigestHelper::sha256(binary).value(), bytes.value());
}

TEST_F(FileDigestTest, StreamsAFileLargerThanItsReadBuffer) {
    const auto path = folder.path() / "content";
    std::ofstream(path, std::ios::binary) << std::string(1000000, 'a');
    const auto hashed = files::FileDigestHelper::sha256(path, 1000000, stopping);
    ASSERT_TRUE(hashed.hasValue());
    EXPECT_EQ(hashed.value(), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    const auto bounded = files::FileDigestHelper::sha256(path, 999999, stopping);
    ASSERT_FALSE(bounded.hasValue());
    EXPECT_EQ(bounded.error().code, "files_digest_too_large");
}

TEST_F(FileDigestTest, RefusesMissingFilesAndDirectories) {
    const auto missing = files::FileDigestHelper::sha256(folder.path() / "missing", 32, stopping);
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.error().code, "files_digest_unavailable");

    const auto directory = files::FileDigestHelper::sha256(folder.path(), 32, stopping);
    ASSERT_FALSE(directory.hasValue());
    EXPECT_EQ(directory.error().code, "files_digest_unavailable");
}

TEST_F(FileDigestTest, CancelsWithoutReturningAPartialDigest) {
    const auto path = folder.path() / "content";
    std::ofstream(path, std::ios::binary) << "abc";
    stopping.store(true);
    const auto hashed = files::FileDigestHelper::sha256(path, 3, stopping);
    ASSERT_FALSE(hashed.hasValue());
    EXPECT_EQ(hashed.error().code, "files_digest_cancelled");
}

} // namespace workpane::tests
