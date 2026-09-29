#include "platform/windows/WindowsText.h"

#include <windows.h>

#include <cstddef>

namespace workpane::platform {

std::wstring WindowsText::wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);

    return result;
}

std::string WindowsText::narrow(const wchar_t* text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);

    if (length <= 1) {
        return {};
    }

    std::string result(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);

    return result;
}

// An argument is quoted the way the C runtime splits a command line, so a path with spaces or quotes reaches the new process intact.
std::wstring WindowsText::quoted(const std::wstring& argument) {
    std::wstring result = L"\"";
    std::size_t backslashes = 0;

    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }

        if (character == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
        } else {
            result.append(backslashes, L'\\');
        }

        backslashes = 0;
        result.push_back(character);
    }

    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');

    return result;
}

} // namespace workpane::platform
