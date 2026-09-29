#include "http/MimeTypes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace workpane::http {

std::string MimeTypes::forExtension(std::string_view extension) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 36> types{{
        {"html", "text/html"}, {"htm", "text/html"}, {"css", "text/css"}, {"js", "text/javascript"}, {"mjs", "text/javascript"}, {"json", "application/json"}, {"map", "application/json"}, {"txt", "text/plain"}, {"md", "text/markdown"}, {"csv", "text/csv"}, {"xml", "application/xml"}, {"svg", "image/svg+xml"}, {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"gif", "image/gif"}, {"webp", "image/webp"}, {"avif", "image/avif"}, {"bmp", "image/bmp"}, {"ico", "image/vnd.microsoft.icon"}, {"woff", "font/woff"}, {"woff2", "font/woff2"}, {"ttf", "font/ttf"}, {"otf", "font/otf"}, {"wasm", "application/wasm"}, {"pdf", "application/pdf"}, {"zip", "application/zip"}, {"mp3", "audio/mpeg"}, {"wav", "audio/wav"}, {"ogg", "audio/ogg"}, {"mp4", "video/mp4"}, {"webm", "video/webm"}, {"glb", "model/gltf-binary"}, {"gltf", "model/gltf+json"}, {"yaml", "application/yaml"}, {"yml", "application/yaml"},
    }};
    std::string lowered(extension.starts_with('.') ? extension.substr(1) : extension);
    // clang-format off
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    const auto found = std::ranges::find_if(types, [&lowered](const auto& entry) { return entry.first == lowered; });
    // clang-format on
    return found == types.end() ? "application/octet-stream" : std::string(found->second);
}

} // namespace workpane::http
