#pragma once

#include "Result.h"
#include "platform/ShellCommand.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::platform {

// The history of one terminal lives in a file of its own, which POSIX shells read from HISTFILE and zsh from startup files the product writes beside it.
// A new history starts from the history of the reader, so the commands typed before are there from the first key, while the commands of the terminal stay in its own file.
// Those startup files read the files of the reader first, so everything the reader configured still applies before the history is moved, and the last one gives the reader back its own ZDOTDIR, so the login files that follow and any zsh started inside are the reader's own.
class ShellHistory final {
  public:
    [[nodiscard]] static Result<std::vector<std::string>> environment(const ShellCommand& shell, const std::filesystem::path& history, const std::vector<std::string>& inherited);

  private:
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 3> zshFiles{{{".zshenv", "if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zshenv\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zshenv\"\n    WORKPANE_USER_ZDOTDIR=\"${ZDOTDIR}\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\nsetopt RCS\n"}, {".zprofile", "if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zprofile\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zprofile\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\n"}, {".zshrc", "if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zshrc\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zshrc\"\nfi\nunsetopt SHARE_HISTORY INC_APPEND_HISTORY_TIME\nsetopt INC_APPEND_HISTORY\nWORKPANE_READER_HISTORY=\"${HISTFILE}\"\nif [[ \"${WORKPANE_READER_HISTORY}\" == \"${WORKPANE_ZDOTDIR}/.zsh_history\" ]]; then\n    WORKPANE_READER_HISTORY=\"${WORKPANE_USER_ZDOTDIR}/.zsh_history\"\nfi\nfc -p \"${WORKPANE_HISTORY_FILE}\" 10000 10000\nif [[ ! -s \"${WORKPANE_HISTORY_FILE}\" && -r \"${WORKPANE_READER_HISTORY}\" ]]; then\n    fc -R \"${WORKPANE_READER_HISTORY}\"\nfi\nif [[ \"${WORKPANE_USER_ZDOTDIR}\" == \"${HOME}\" ]]; then\n    unset ZDOTDIR\nelse\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\nfi\nunset WORKPANE_HISTORY_FILE WORKPANE_READER_HISTORY WORKPANE_USER_ZDOTDIR WORKPANE_ZDOTDIR\n"}}};

    [[nodiscard]] static Result<void> write(const std::filesystem::path& directory);
    static void seed(const std::filesystem::path& history, const std::vector<std::string>& inherited);
};

} // namespace workpane::platform
