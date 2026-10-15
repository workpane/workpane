#include "platform/TerminalEnvironment.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

// Answers the inherited environment without what describes another terminal, then the variables of this terminal, UTF-8 for a shell started without any locale, since that is what the emulator writes and draws, and the additions of one terminal over all of it.
std::vector<std::string> TerminalEnvironment::build(const std::vector<std::string>& inherited, const std::vector<std::string>& additions) {
    constexpr std::array<std::string_view, 5> replacedByTerminal{"TERM", "COLORTERM", "TERM_PROGRAM", "HISTFILE", "ZDOTDIR"};
    constexpr std::array<std::string_view, 3> locales{"LANG", "LC_ALL", "LC_CTYPE"};
    const std::optional<std::string> folder = readerZshFolder(inherited);
    std::vector<std::string> environment;
    bool localized = false;

    for (const auto& entry : inherited) {
        const std::string variable = name(entry);
        // clang-format off
        const bool replaced = std::ranges::any_of(additions, [&variable](const std::string& added) { return name(added) == variable; });
        // clang-format on

        if (replaced || variable.starts_with("WORKPANE_") || std::ranges::find(replacedByTerminal, variable) != replacedByTerminal.end()) {
            continue;
        }

        localized = localized || std::ranges::find(locales, variable) != locales.end();
        environment.push_back(entry);
    }

    environment.emplace_back("TERM=xterm-256color");
    environment.emplace_back("COLORTERM=truecolor");
    environment.emplace_back("TERM_PROGRAM=Workpane");

    if (!localized) {
        environment.emplace_back("LANG=en_US.UTF-8");
    }

    // clang-format off
    const bool relocated = std::ranges::any_of(additions, [](const std::string& added) { return name(added) == "ZDOTDIR"; });
    // clang-format on

    if (folder.has_value() && !relocated) {
        environment.push_back("ZDOTDIR=" + *folder);
    }

    environment.insert(environment.end(), additions.begin(), additions.end());

    return environment;
}

// Answers the folder zsh reads the startup files of the reader from, which is the home folder unless the reader chose another one.
std::string TerminalEnvironment::zshFolder(const std::vector<std::string>& inherited) {
    return readerZshFolder(inherited).value_or(home(inherited));
}

std::string TerminalEnvironment::home(const std::vector<std::string>& inherited) {
    return value(inherited, "HOME").value_or("");
}

std::string TerminalEnvironment::name(const std::string& entry) {
    return entry.substr(0, entry.find('='));
}

std::optional<std::string> TerminalEnvironment::value(const std::vector<std::string>& environment, std::string_view variable) {
    for (const auto& entry : environment) {
        if (name(entry) == variable) {
            return entry.substr(entry.find('=') + 1);
        }
    }

    return std::nullopt;
}

// A ZDOTDIR naming the integration folder of the Workpane terminal the product was started from gives way to the folder of the reader that terminal recorded.
std::optional<std::string> TerminalEnvironment::readerZshFolder(const std::vector<std::string>& inherited) {
    const std::optional<std::string> folder = value(inherited, "ZDOTDIR");
    const std::optional<std::string> integration = value(inherited, "WORKPANE_ZDOTDIR");

    if (!folder.has_value() || folder->empty()) {
        return std::nullopt;
    }

    if (folder != integration) {
        return folder;
    }

    const std::optional<std::string> recorded = value(inherited, "WORKPANE_USER_ZDOTDIR");

    if (!recorded.has_value() || recorded->empty() || recorded == value(inherited, "HOME")) {
        return std::nullopt;
    }

    return recorded;
}

} // namespace workpane::platform
