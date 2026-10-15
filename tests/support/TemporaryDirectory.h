#pragma once

#include <filesystem>

namespace workpane::tests {

// A directory of its own for one test, removed with everything in it when the test ends.
class TemporaryDirectory final {
  public:
    explicit TemporaryDirectory(const std::filesystem::path& parent = std::filesystem::temp_directory_path());
    ~TemporaryDirectory();

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const;

  private:
    std::filesystem::path m_path;
};

} // namespace workpane::tests
