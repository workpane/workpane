#include "platform/NativeProcesses.h"

#include "platform/windows/WindowsProcess.h"
#include "platform/windows/WindowsText.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cwctype>
#include <string>
#include <system_error>
#include <utility>

namespace workpane::platform {

Result<std::unique_ptr<process::Process>> NativeProcesses::start(const process::ProcessLaunch& launch, process::ProcessEvents events) {
    return WindowsProcess::start(launch, std::move(events));
}

// A bare name is tried with every extension Windows runs by itself, on the search path and then in the directories a plugin adds.
std::optional<std::filesystem::path> NativeProcesses::find(std::string_view name, const std::vector<std::filesystem::path>& directories) const {
    const std::filesystem::path named(WindowsText::wide(name));

    if (name.empty() || named.is_absolute()) {
        return name.empty() ? std::nullopt : executable(named);
    }

    if (name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> searched = searchPath();
    searched.insert(searched.end(), directories.begin(), directories.end());

    for (const auto& directory : searched) {
        if (auto found = executable(directory / named); found.has_value()) {
            return found;
        }
    }

    return std::nullopt;
}

std::vector<std::filesystem::path> NativeProcesses::searchPath() {
    std::vector<std::filesystem::path> directories;
    const DWORD length = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring value(length, L'\0');
    value.resize(GetEnvironmentVariableW(L"PATH", value.data(), length));
    std::wstring_view remaining(value);

    while (!remaining.empty()) {
        const std::size_t separator = remaining.find(L';');
        const std::wstring_view entry = remaining.substr(0, separator);

        if (!entry.empty()) {
            directories.emplace_back(entry);
        }

        remaining = separator == std::wstring_view::npos ? std::wstring_view() : remaining.substr(separator + 1);
    }

    return directories;
}

// A name that already carries an extension Windows runs is taken as it is, and any other name is tried with each of those extensions.
std::optional<std::filesystem::path> NativeProcesses::executable(const std::filesystem::path& candidate) {
    constexpr std::array<std::wstring_view, 4> runnable{L".exe", L".cmd", L".bat", L".com"};
    std::wstring extension = candidate.extension().wstring();
    // clang-format off
    std::ranges::transform(extension, extension.begin(), [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    // clang-format on
    std::error_code error;

    if (std::ranges::find(runnable, extension) != runnable.end()) {
        return std::filesystem::is_regular_file(candidate, error) ? std::optional<std::filesystem::path>(candidate) : std::nullopt;
    }

    for (const auto suffix : runnable) {
        std::filesystem::path extended = candidate;
        extended += suffix;

        if (std::filesystem::is_regular_file(extended, error)) {
            return extended;
        }
    }

    return std::nullopt;
}

} // namespace workpane::platform
