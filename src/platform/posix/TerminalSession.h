#pragma once

#include "Result.h"

#include <filesystem>

namespace workpane::platform {

class TerminalSession final {
  public:
    [[nodiscard]] static Result<std::filesystem::path> program();
    [[nodiscard]] static int run(int argc, char** argv);
};

} // namespace workpane::platform
