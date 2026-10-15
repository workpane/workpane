#include "app/ApplicationIdentity.h"

#include "text/Utf8Helper.h"

#include <array>
#include <utility>

namespace workpane::app {

bool ApplicationIdentity::validText(std::string_view value) {
    if (value.empty() || !text::Utf8Helper::valid(value)) {
        return false;
    }

    for (const char character : value) {
        if (static_cast<unsigned char>(character) < 32 || character == 127) {
            return false;
        }
    }

    return true;
}

bool ApplicationIdentity::validSegment(std::string_view value) {
    if (!validText(value) || value == "." || value == "..") {
        return false;
    }

    return value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == std::string_view::npos;
}

Result<void> ApplicationIdentity::validate() const {
    const std::array<std::pair<std::string_view, const std::string*>, 4> texts{{{"name", &name}, {"version", &version}, {"buildNumber", &buildNumber}, {"organization", &organization}}};

    for (const auto& [field, value] : texts) {
        if (!validText(*value)) {
            return Result<void>::failure({"application_identity_invalid", "The application identity contains invalid text", std::string(field)});
        }
    }

    const std::array<std::pair<std::string_view, const std::string*>, 3> segments{{{"identifier", &identifier}, {"resourceDirectory", &resourceDirectory}, {"dataDirectory", &dataDirectory}}};

    for (const auto& [field, value] : segments) {
        if (!validSegment(*value)) {
            return Result<void>::failure({"application_identity_invalid", "The application identity contains an invalid directory or identifier", std::string(field)});
        }
    }

    return Result<void>::success();
}

} // namespace workpane::app
