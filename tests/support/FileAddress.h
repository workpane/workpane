#pragma once

#include <filesystem>
#include <string>

namespace workpane::tests {

// Writes the file address of an absolute path the way the protocols of language and MCP servers read it, with the slash a Windows drive needs.
class FileAddress final {
  public:
    [[nodiscard]] static std::string of(const std::filesystem::path& path);
};

} // namespace workpane::tests
