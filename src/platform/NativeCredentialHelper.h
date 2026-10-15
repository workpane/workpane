#pragma once

#include "Result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace workpane::platform {

// Stores credentials in the native account's protected credential service.
class NativeCredentialHelper final {
  public:
    static constexpr std::size_t maximumValueBytes{1023};

    [[nodiscard]] static Result<void> validateValue(std::string_view value);
    [[nodiscard]] static Result<std::optional<std::string>> read(std::string_view service, std::string_view account);
    [[nodiscard]] static Result<void> write(std::string_view service, std::string_view account, std::string_view value);
    [[nodiscard]] static Result<void> erase(std::string_view service, std::string_view account);
};

} // namespace workpane::platform
