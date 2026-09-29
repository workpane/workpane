#include "platform/windows/WindowsEnvironment.h"

#include "platform/windows/WindowsText.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace workpane::platform {

std::vector<std::string> WindowsEnvironment::inherited() {
    std::vector<std::string> entries;
    wchar_t* strings = GetEnvironmentStringsW();

    for (const wchar_t* entry = strings; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
        entries.push_back(WindowsText::narrow(entry));
    }

    FreeEnvironmentStringsW(strings);

    return entries;
}

// A name that starts with an equals sign, such as the ones naming the directory of a drive, keeps it as part of its name.
std::wstring WindowsEnvironment::block(const std::vector<std::string>& entries) {
    std::vector<std::wstring> wide;

    for (const auto& entry : entries) {
        wide.push_back(WindowsText::wide(entry));
    }

    // clang-format off
    const auto folded = [](const std::wstring& entry) { std::wstring name = entry.substr(0, entry.find(L'=', 1)); std::ranges::transform(name, name.begin(), [](wchar_t character) { return static_cast<wchar_t>(std::towupper(character)); }); return name; };
    std::ranges::stable_sort(wide, [&folded](const std::wstring& first, const std::wstring& second) { return folded(first) < folded(second); });
    // clang-format on
    std::wstring joined;

    for (const auto& entry : wide) {
        joined += entry;
        joined.push_back(L'\0');
    }

    joined.push_back(L'\0');

    return joined;
}

} // namespace workpane::platform
