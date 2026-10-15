#pragma once

#include <string>
#include <string_view>

namespace workpane::http {

// Names the media type of a file by its extension, and a file of an unknown kind is served as bytes.
class MimeTypes final {
  public:
    [[nodiscard]] static std::string forExtension(std::string_view extension);
};

} // namespace workpane::http
