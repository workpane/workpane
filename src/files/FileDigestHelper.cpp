#include "files/FileDigestHelper.h"

#include <openssl/evp.h>

#include <array>
#include <fstream>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>

namespace workpane::files {

Result<std::string> FileDigestHelper::sha256(const std::filesystem::path& path, std::size_t maximum, const std::atomic<bool>& stopping) {
    std::error_code error;
    const bool regular = std::filesystem::is_regular_file(path, error);

    if (error || !regular) {
        return Result<std::string>::failure({"files_digest_unavailable", "The file cannot be read for its digest", ""});
    }

    std::ifstream input(path, std::ios::binary);
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);

    if (!input || !context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        return Result<std::string>::failure({"files_digest_failed", "The file digest could not be started", ""});
    }

    std::array<char, 65536> buffer{};
    std::size_t bytes = 0;

    while (true) {
        if (stopping.load()) {
            return Result<std::string>::failure({"files_digest_cancelled", "The file digest was cancelled", ""});
        }

        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(input.gcount());

        if (count > maximum - bytes) {
            return Result<std::string>::failure({"files_digest_too_large", "The file exceeds the digest size bound", ""});
        }

        if (input.bad() || (input.fail() && !input.eof()) || EVP_DigestUpdate(context.get(), buffer.data(), count) != 1) {
            return Result<std::string>::failure({"files_digest_failed", "The file could not be read whole for its digest", ""});
        }

        bytes += count;

        if (input.eof()) {
            break;
        }
    }

    std::array<unsigned char, 32> digest{};
    unsigned int length = 0;

    if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1 || length != digest.size()) {
        return Result<std::string>::failure({"files_digest_failed", "The file digest could not be completed", ""});
    }

    return Result<std::string>::success(encode(digest));
}

Result<std::string> FileDigestHelper::sha256(std::string_view bytes) {
    std::array<unsigned char, 32> digest{};
    unsigned int length = 0;

    if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1 || length != digest.size()) {
        return Result<std::string>::failure({"files_digest_failed", "The file digest could not be completed", ""});
    }

    return Result<std::string>::success(encode(digest));
}

std::string FileDigestHelper::encode(std::span<const unsigned char> digest) {
    constexpr std::string_view alphabet{"0123456789abcdef"};
    std::string encoded;
    encoded.reserve(digest.size() * 2);

    for (const auto byte : digest) {
        encoded.push_back(alphabet[byte >> 4U]);
        encoded.push_back(alphabet[byte & 15U]);
    }

    return encoded;
}

} // namespace workpane::files
