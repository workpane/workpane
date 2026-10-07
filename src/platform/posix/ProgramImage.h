#pragma once

#include "Result.h"

#include <sys/types.h>

#include <array>
#include <string>
#include <vector>

namespace workpane::platform {

// The program, arguments, environment and folder of a program the product starts, which the system starts in one step without copying the product.
// A program starts with the signals of a shell, with no descriptor of the product but the streams it is given, and in a process group or a session of its own, so stopping it stops what it started.
class ProgramImage final {
  public:
    enum class Grouping { Group, Session };

    ProgramImage(const std::vector<std::string>& arguments, const std::vector<std::string>& environment, std::string directory);

    ProgramImage(const ProgramImage&) = delete;
    ProgramImage& operator=(const ProgramImage&) = delete;

    [[nodiscard]] static std::vector<std::string> inheritedEnvironment();
    [[nodiscard]] Result<pid_t> spawn(const std::array<int, 3>& streams, Grouping grouping) const;
    [[nodiscard]] Result<pid_t> spawnOnTerminal(const std::string& terminal) const;

  private:
    [[nodiscard]] static std::vector<std::vector<char>> terminated(const std::vector<std::string>& texts);
    [[nodiscard]] static std::vector<char*> pointers(std::vector<std::vector<char>>& texts);
    [[nodiscard]] Result<pid_t> start(const std::array<int, 3>& streams, const std::string& terminal, Grouping grouping) const;

    std::vector<std::vector<char>> m_arguments;
    std::vector<std::vector<char>> m_environment;
    std::vector<char*> m_argv;
    std::vector<char*> m_envp;
    std::string m_directory;
};

} // namespace workpane::platform
