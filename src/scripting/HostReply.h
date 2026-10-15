#pragma once

#include "Error.h"

#include <nlohmann/json.hpp>

namespace workpane::scripting {

// Every host function answers an object carrying either its value or the structured error that refused it.
class HostReply final {
  public:
    [[nodiscard]] static nlohmann::json success(nlohmann::json value = nullptr);
    [[nodiscard]] static nlohmann::json failure(const Error& error);
};

} // namespace workpane::scripting
