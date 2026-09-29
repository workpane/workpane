#include "platform/posix/ShellHistory.h"

#include "platform/FileReplacement.h"
#include "platform/TerminalEnvironment.h"

#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace workpane::platform {

// A terminal without a history file of its own keeps the history the shell would keep anyway.
Result<std::vector<std::string>> ShellHistory::environment(const ShellCommand& shell, const std::filesystem::path& history, const std::vector<std::string>& inherited) {
    if (history.empty()) {
        return Result<std::vector<std::string>>::success({});
    }

    std::error_code error;
    std::filesystem::create_directories(history.parent_path(), error);

    if (error) {
        return Result<std::vector<std::string>>::failure({"terminal_history_unavailable", "The folder of the history of the terminal could not be created", history.parent_path().string() + ": " + error.message()});
    }

    std::vector<std::string> entries{"HISTFILE=" + history.string()};

    if (shell.name != "zsh") {
        return Result<std::vector<std::string>>::success(std::move(entries));
    }

    const std::filesystem::path startup = history.parent_path() / "zsh";

    if (const auto written = write(startup); !written.hasValue()) {
        return Result<std::vector<std::string>>::failure(written.error());
    }

    entries.push_back("WORKPANE_HISTORY_FILE=" + history.string());
    entries.push_back("WORKPANE_USER_ZDOTDIR=" + TerminalEnvironment::zshFolder(inherited));
    entries.push_back("WORKPANE_ZDOTDIR=" + startup.string());
    entries.push_back("ZDOTDIR=" + startup.string());

    return Result<std::vector<std::string>>::success(std::move(entries));
}

// A startup file already holding its content is left alone, and one that differs is replaced whole, so a zsh reading it while another terminal starts never reads a file half written.
Result<void> ShellHistory::write(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);

    if (error) {
        return Result<void>::failure({"terminal_history_unavailable", "The folder of the startup files of the terminal could not be created", directory.string()});
    }

    for (const auto& [name, content] : zshFiles) {
        const std::filesystem::path target = directory / name;
        std::ifstream current(target, std::ios::binary);
        const std::string stored((std::istreambuf_iterator<char>(current)), std::istreambuf_iterator<char>());

        if (current && stored == content) {
            continue;
        }

        std::filesystem::path staged = target;
        staged += ".staged";
        std::ofstream file(staged, std::ios::binary | std::ios::trunc);
        file << content;
        file.close();

        // A staged file that could not be written whole is dropped, so a full disk never replaces a good startup file with a part of one.
        if (!file || FileReplacement::replace(staged, target)) {
            std::filesystem::remove(staged, error);
            return Result<void>::failure({"terminal_history_unavailable", "The startup files that keep the history of the terminal could not be written", target.string()});
        }
    }

    return Result<void>::success();
}

} // namespace workpane::platform
