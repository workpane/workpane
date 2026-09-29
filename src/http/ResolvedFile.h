#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace workpane::http {

struct ResolvedFile final {
    std::filesystem::path path;
    std::uintmax_t size{0};
    std::string mimeType;
};

} // namespace workpane::http
