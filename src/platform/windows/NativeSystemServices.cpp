#include "platform/NativeSystemServices.h"

#include "platform/TimeZoneDatabase.h"
#include "platform/UrlPolicy.h"
#include "platform/windows/WindowsFonts.h"
#include "platform/windows/WindowsText.h"

#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

Result<void> NativeSystemServices::openUrl(std::string_view url) {
    if (!UrlPolicy::allowed(url)) {
        return Result<void>::failure({"system_url_refused", "Only web addresses open in the default browser", std::string(url)});
    }

    // The shell expects an apartment on the thread that asks it, and the worker running this request starts with none.
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const std::wstring address = WindowsText::wide(url);
    const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", address.c_str(), nullptr, nullptr, SW_SHOWNORMAL));

    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }

    if (opened <= 32) {
        return Result<void>::failure({"system_url_failed", "The default browser could not open the address", std::string(url)});
    }

    return Result<void>::success();
}

Result<void> NativeSystemServices::revealPath(const std::filesystem::path& path) {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const std::wstring parameters = L"/select," + WindowsText::quoted(path.wstring());
    const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL));

    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }

    if (opened <= 32) {
        return Result<void>::failure({"system_reveal_failed", "The path could not be shown in the file manager", path.string()});
    }

    return Result<void>::success();
}

std::string NativeSystemServices::locale() {
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> name{};

    if (GetUserDefaultLocaleName(name.data(), static_cast<int>(name.size())) == 0) {
        return {};
    }

    return WindowsText::narrow(name.data());
}

std::filesystem::path NativeSystemServices::home() {
    PWSTR folder = nullptr;

    if (FAILED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &folder))) {
        return std::filesystem::path(L"C:\\");
    }

    std::filesystem::path profile(folder);
    CoTaskMemFree(folder);

    return profile;
}

// The downloads folder is the known folder Windows keeps for the account, which a moved folder follows.
std::filesystem::path NativeSystemServices::downloads() {
    PWSTR folder = nullptr;

    if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &folder))) {
        CoTaskMemFree(folder);
        return home() / L"Downloads";
    }

    std::filesystem::path downloads(folder);
    CoTaskMemFree(folder);

    return downloads;
}

// The zone the system is set to, named as the time zone database names it, such as `Europe/Lisbon`.
Result<std::string> NativeSystemServices::timeZone() {
    return TimeZoneDatabase::current();
}

std::int64_t NativeSystemServices::processId() {
    return static_cast<std::int64_t>(GetCurrentProcessId());
}

std::vector<InstalledFont> NativeSystemServices::monospaceFonts() {
    return WindowsFonts::monospace();
}

Result<int> NativeSystemServices::zoneOffset(std::string_view zone, std::int64_t seconds) {
    return TimeZoneDatabase::offset(zone, seconds);
}

Result<void> NativeSystemServices::relaunch(const std::vector<std::string>& arguments) {
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));

    if (length == 0 || length >= executable.size()) {
        return Result<void>::failure({"system_relaunch_failed", "The product executable could not be located", {}});
    }

    std::wstring command = WindowsText::quoted(executable.data());

    for (const auto& argument : arguments) {
        command += L" " + WindowsText::quoted(WindowsText::wide(argument));
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup, &process) == 0) {
        return Result<void>::failure({"system_relaunch_failed", "The product could not be started again", WindowsText::narrow(executable.data())});
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    return Result<void>::success();
}

} // namespace workpane::platform
