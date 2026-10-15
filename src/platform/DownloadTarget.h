#pragma once

#include <filesystem>
#include <string_view>

namespace workpane::platform {

// Chooses the file a download is saved in: inside the downloads folder, created when missing, under the plain name the page suggests, with a counter before its extension when a file of that name exists.
class DownloadTarget final {
  public:
    [[nodiscard]] static std::filesystem::path choose(const std::filesystem::path& folder, std::string_view suggested);

  private:
    static constexpr std::string_view unnamed{"download"};
};

} // namespace workpane::platform
