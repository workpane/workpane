#include "http/StaticFileResolver.h"

#include "http/MimeTypes.h"

#include <cctype>
#include <fstream>
#include <system_error>

namespace workpane::http {

// A document root is the canonical form of a folder the product can read, so a link inside it cannot move it later.
Result<std::filesystem::path> StaticFileResolver::canonicalRoot(const std::filesystem::path& root) {
    std::error_code failure;
    const std::filesystem::path canonical = std::filesystem::canonical(root, failure);

    if (failure || !root.is_absolute() || !std::filesystem::is_directory(canonical, failure)) {
        return Result<std::filesystem::path>::failure({"http_root_invalid", "The document root is not a readable folder", root.string()});
    }

    std::filesystem::directory_iterator probe(canonical, failure);

    if (failure) {
        return Result<std::filesystem::path>::failure({"http_root_invalid", "The document root is not a readable folder", root.string()});
    }

    return Result<std::filesystem::path>::success(canonical);
}

// A malformed target is a bad request, and a well formed one naming nothing readable inside the root is a missing file.
Result<ResolvedFile> StaticFileResolver::resolve(const std::filesystem::path& root, std::string_view target) {
    const Error invalid{"http_request_invalid", "The request names a path the server refuses", std::string(target.substr(0, 256))};
    const Error missing{"http_file_missing", "The request names no readable file inside the document root", std::string(target.substr(0, 256))};

    if (target.size() > maximumTargetLength || target.find('\0') != std::string_view::npos || !target.starts_with('/')) {
        return Result<ResolvedFile>::failure(invalid);
    }

    const auto decoded = decode(target.substr(0, target.find_first_of("?#")));

    if (!decoded.has_value() || decoded->find('\0') != std::string::npos || decoded->find('\\') != std::string::npos) {
        return Result<ResolvedFile>::failure(invalid);
    }

    const std::filesystem::path relative = std::filesystem::path(decoded->substr(decoded->find_first_not_of('/') == std::string::npos ? decoded->size() : decoded->find_first_not_of('/'))).lexically_normal();

    if (relative.is_absolute() || relative.has_root_name() || (!relative.empty() && *relative.begin() == "..")) {
        return Result<ResolvedFile>::failure(invalid);
    }

    std::error_code failure;
    std::filesystem::path candidate = root / relative;

    if (std::filesystem::is_directory(candidate, failure)) {
        const bool html = std::filesystem::is_regular_file(candidate / "index.html", failure);
        candidate = html ? candidate / "index.html" : candidate / "index.htm";
    }

    const std::filesystem::path canonical = std::filesystem::canonical(candidate, failure);

    if (failure || !inside(root, canonical) || !std::filesystem::is_regular_file(canonical, failure)) {
        return Result<ResolvedFile>::failure(missing);
    }

    const std::uintmax_t size = std::filesystem::file_size(canonical, failure);
    std::ifstream readable(canonical, std::ios::binary);

    if (failure || size > maximumFileSize || !readable) {
        return Result<ResolvedFile>::failure(missing);
    }

    return Result<ResolvedFile>::success({canonical, size, MimeTypes::forExtension(canonical.extension().string())});
}

std::optional<std::string> StaticFileResolver::decode(std::string_view path) {
    std::string decoded;

    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index] != '%') {
            decoded += path[index];
            continue;
        }

        if (index + 2 >= path.size() || std::isxdigit(static_cast<unsigned char>(path[index + 1])) == 0 || std::isxdigit(static_cast<unsigned char>(path[index + 2])) == 0) {
            return std::nullopt;
        }

        decoded += static_cast<char>(std::stoi(std::string(path.substr(index + 1, 2)), nullptr, 16));
        index += 2;
    }

    return decoded;
}

bool StaticFileResolver::inside(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const std::string base = root.generic_string();
    const std::string path = candidate.generic_string();
    return path == base || path.starts_with(base.ends_with('/') ? base : base + "/");
}

} // namespace workpane::http
