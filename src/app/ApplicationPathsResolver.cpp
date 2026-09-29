#include "app/ApplicationPathsResolver.h"

#if defined(_WIN32)
#include <shlobj.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace workpane::app {

Result<ApplicationPaths> ApplicationPathsResolver::resolve(const std::optional<std::filesystem::path>& dataOverride) {
    auto running = executable();

    if (!running.hasValue()) {
        return Result<ApplicationPaths>::failure(running.error());
    }

    const std::filesystem::path resources = resourcesFor(running.value());
    std::error_code error;

    if (!std::filesystem::is_directory(resources / "lua", error)) {
        return Result<ApplicationPaths>::failure({"resources_missing", "The product resources were not found beside the executable", resources.string()});
    }

    if (dataOverride.has_value()) {
        return Result<ApplicationPaths>::success({running.value(), resources, *dataOverride});
    }

    auto data = platformData();

    if (!data.hasValue()) {
        return Result<ApplicationPaths>::failure(data.error());
    }

    return Result<ApplicationPaths>::success({running.value(), resources, data.value()});
}

Result<std::filesystem::path> ApplicationPathsResolver::executable() {
#if defined(_WIN32)
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

    if (length == 0 || length >= buffer.size()) {
        return Result<std::filesystem::path>::failure({"executable_unknown", "The product executable could not be located", {}});
    }

    return Result<std::filesystem::path>::success(std::filesystem::path(std::wstring(buffer.data(), length)));
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');

    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return Result<std::filesystem::path>::failure({"executable_unknown", "The product executable could not be located", {}});
    }

    std::error_code error;
    const auto canonical = std::filesystem::canonical(std::filesystem::path(buffer.c_str()), error);
    return error ? Result<std::filesystem::path>::failure({"executable_unknown", "The product executable could not be resolved", error.message()}) : Result<std::filesystem::path>::success(canonical);
#else
    std::error_code error;
    const auto canonical = std::filesystem::canonical("/proc/self/exe", error);
    return error ? Result<std::filesystem::path>::failure({"executable_unknown", "The product executable could not be located", error.message()}) : Result<std::filesystem::path>::success(canonical);
#endif
}

// A macOS bundle keeps its resources in its own directory, and every other layout keeps them in the shared data directory beside the executable.
std::filesystem::path ApplicationPathsResolver::resourcesFor(const std::filesystem::path& executable) {
    const std::filesystem::path directory = executable.parent_path();

    if (directory.filename() == "MacOS" && directory.parent_path().filename() == "Contents") {
        return directory.parent_path() / "Resources";
    }

    return (directory.parent_path() / "share" / "workpane").lexically_normal();
}

Result<std::filesystem::path> ApplicationPathsResolver::platformData() {
#if defined(_WIN32)
    PWSTR folder = nullptr;

    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder) != S_OK) {
        CoTaskMemFree(folder);
        return Result<std::filesystem::path>::failure({"data_directory_unknown", "The local application data folder could not be located", {}});
    }

    const std::filesystem::path directory = std::filesystem::path(folder) / "Workpane";
    CoTaskMemFree(folder);

    return Result<std::filesystem::path>::success(directory);
#else
    const char* home = std::getenv("HOME");

    if (home == nullptr || home[0] == '\0') {
        return Result<std::filesystem::path>::failure({"data_directory_unknown", "The home directory is not known to the process", {}});
    }

#if defined(__APPLE__)
    return Result<std::filesystem::path>::success(std::filesystem::path(home) / "Library" / "Application Support" / "Workpane");
#else
    // The data directory of the desktop specification is preferred, and the home fallback it defines applies only when it is not set.
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const std::filesystem::path base = xdg != nullptr && xdg[0] == '/' ? std::filesystem::path(xdg) : std::filesystem::path(home) / ".local" / "share";
    return Result<std::filesystem::path>::success(base / "workpane");
#endif
#endif
}

} // namespace workpane::app
