#include "platform/NativeCredentialHelper.h"

#include "text/Utf8Helper.h"

#include <string>
#include <string_view>

namespace workpane::platform {

Result<void> NativeCredentialHelper::validateValue(std::string_view value) {
    if (value.empty() || value.size() > maximumValueBytes || value.find_first_of("\r\n") != std::string_view::npos || value.find('\0') != std::string_view::npos || !text::Utf8Helper::valid(value)) {
        return Result<void>::failure({"credential_request_invalid", "The credential value is invalid", ""});
    }

    return Result<void>::success();
}

} // namespace workpane::platform
