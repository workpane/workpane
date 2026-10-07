#include "platform/windows/WindowsRegistry.h"

#include "platform/windows/WindowsTextHelper.h"

#include <windows.h>

#include <vector>

namespace workpane::platform {

std::string WindowsRegistry::text(const wchar_t* key, const wchar_t* value) {
    DWORD size = 0;

    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0) {
        return {};
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');

    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, buffer.data(), &size) != ERROR_SUCCESS) {
        return {};
    }

    return WindowsTextHelper::narrow(buffer.data());
}

std::uint32_t WindowsRegistry::number(const wchar_t* key, const wchar_t* value) {
    DWORD read = 0;
    DWORD size = sizeof(read);

    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_DWORD, nullptr, &read, &size) != ERROR_SUCCESS) {
        return 0;
    }

    return read;
}

} // namespace workpane::platform
