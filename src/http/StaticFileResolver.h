#pragma once

#include "Result.h"
#include "http/ResolvedFile.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace workpane::http {

// Turns the target of a request into a readable file inside the document root, and refuses every target that would leave it.
class StaticFileResolver final {
  public:
    [[nodiscard]] static Result<std::filesystem::path> canonicalRoot(const std::filesystem::path& root);
    [[nodiscard]] static Result<ResolvedFile> resolve(const std::filesystem::path& root, std::string_view target);

  private:
    static constexpr std::size_t maximumTargetLength{8192};
    static constexpr std::uintmax_t maximumFileSize{256ULL * 1024 * 1024};

    [[nodiscard]] static std::optional<std::string> decode(std::string_view path);
    [[nodiscard]] static bool inside(const std::filesystem::path& root, const std::filesystem::path& candidate);
};

} // namespace workpane::http
