#include "platform/NativePseudoTerminals.h"

#include "platform/NativeProcesses.h"
#include "platform/PathText.h"
#include "platform/windows/WindowsPseudoTerminal.h"
#include "platform/windows/WindowsText.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::platform {

// The consoles of closed terminals are closed already, so the wait lasts at most the grace of the last shell.
NativePseudoTerminals::~NativePseudoTerminals() {
    m_threads->wait();
}

void NativePseudoTerminals::listen(std::function<void()> arrived) {
    m_arrived = std::move(arrived);
}

// A terminal names its own shell or starts the one found first, and keeps its commands in a history file of its own, which PowerShell learns through PSReadLine and a POSIX shell through its variable, while the command prompt keeps no history file.
Result<std::unique_ptr<ui::PseudoTerminal>> NativePseudoTerminals::start(const ui::TerminalLaunch& launch) {
    const std::filesystem::path chosen = launch.shell.empty() ? program() : launch.shell;
    ShellCommand shell = command(chosen);
    std::vector<std::string> additions;

    if (!launch.historyFile.empty() && (shell.name == "pwsh" || shell.name == "powershell")) {
        std::string quoted = quotePaths({launch.historyFile}, chosen);
        quoted.pop_back();
        shell.arguments.back() += "; if (Get-Module -ListAvailable PSReadLine) { Set-PSReadLineOption -HistorySavePath " + quoted + " }";
    }

    if (!launch.historyFile.empty() && posixShell(shell.name)) {
        additions.push_back("HISTFILE=" + PathText::generic(launch.historyFile));
    }

    return WindowsPseudoTerminal::start(shell, launch, additions, m_arrived, m_threads);
}

std::filesystem::path NativePseudoTerminals::shellProgram() const {
    return program();
}

// PowerShell reads a single quoted path literally with each of its single quotes doubled, the typographic ones included, a POSIX shell such as Git Bash reads one literally with each quote closed, escaped and opened again, and the command prompt reads a double quoted one, since no Windows path holds a double quote.
std::string NativePseudoTerminals::quotePaths(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& shell) const {
    const std::string name = command(shell.empty() ? program() : shell).name;
    const bool powershell = name == "pwsh" || name == "powershell";
    const bool posix = posixShell(name);
    const std::wstring quote = powershell || posix ? L"'" : L"\"";
    std::wstring quoted;

    for (const auto& path : paths) {
        std::wstring escaped = quote;

        for (const wchar_t character : path.wstring()) {
            if (posix && character == L'\'') {
                escaped += L"'\\''";
                continue;
            }

            escaped += character;

            if (powershell && (character == L'\'' || (character >= L'\u2018' && character <= L'\u201B'))) {
                escaped += character;
            }
        }

        quoted += escaped + quote + L" ";
    }

    return WindowsText::narrow(quoted.c_str());
}

// PowerShell 7 comes first, then Windows PowerShell and then the command prompt, each looked up on the search path alone, never in the folder the product started from.
std::filesystem::path NativePseudoTerminals::program() {
    const NativeProcesses processes;

    for (const std::string_view candidate : {"pwsh.exe", "powershell.exe"}) {
        if (auto found = processes.find(candidate, {}); found.has_value()) {
            return *found;
        }
    }

    std::array<wchar_t, MAX_PATH> prompt{};
    const DWORD length = GetEnvironmentVariableW(L"COMSPEC", prompt.data(), static_cast<DWORD>(prompt.size()));
    return length > 0 ? std::filesystem::path(prompt.data()) : std::filesystem::path(L"C:\\Windows\\System32\\cmd.exe");
}

// PowerShell loads the profiles of the reader and wraps their prompt so every prompt marks the directory it stands in.
// A POSIX shell such as Git Bash starts as an interactive login shell, as Windows Terminal starts it, so it reads the profile of its system and of the reader, and any other shell starts as it is.
ShellCommand NativePseudoTerminals::command(const std::filesystem::path& program) {
    const std::wstring integration = L"$global:WorkpanePrompt = $function:prompt; function global:prompt { $location = (Get-Location).ProviderPath -replace '\\\\', '/'; [Console]::Write(\"$([char]27)]7;file://localhost/$location$([char]7)\"); & $global:WorkpanePrompt }";
    std::wstring stem = program.stem().wstring();
    // clang-format off
    std::ranges::transform(stem, stem.begin(), [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    // clang-format on
    const std::string name = WindowsText::narrow(stem.c_str());
    const bool powershell = name == "pwsh" || name == "powershell";

    if (powershell) {
        return {WindowsText::narrow(program.c_str()), {"-NoLogo", "-NoExit", "-Command", WindowsText::narrow(integration.c_str())}, name};
    }

    return {WindowsText::narrow(program.c_str()), posixShell(name) ? std::vector<std::string>{"-l", "-i"} : std::vector<std::string>{}, name};
}

bool NativePseudoTerminals::posixShell(std::string_view name) {
    return name == "bash" || name == "zsh" || name == "fish" || name == "sh";
}

} // namespace workpane::platform
