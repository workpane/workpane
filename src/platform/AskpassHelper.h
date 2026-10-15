#pragma once

#include "Result.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace workpane::platform {

class AskpassHelper final {
  public:
    [[nodiscard]] static Result<std::string> request(int port, std::string_view token, std::string_view prompt, std::string_view kind);
    [[nodiscard]] static int run(int argc, char** argv);

  private:
    static constexpr std::size_t maximumPrompt{16384};
    static constexpr std::size_t maximumAnswer{1023};
    static constexpr std::size_t maximumEnvironment{128};

    [[nodiscard]] static Result<std::string> refused();
    [[nodiscard]] static Result<std::string> environment(const char* name);
};

} // namespace workpane::platform
