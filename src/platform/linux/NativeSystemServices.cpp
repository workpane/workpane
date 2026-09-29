#include "platform/NativeSystemServices.h"

#include "platform/TimeZoneDatabase.h"
#include "platform/UrlPolicy.h"
#include "platform/linux/LinuxProcess.h"

#include <fontconfig/fontconfig.h>
#include <glib.h>

#include <pwd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace workpane::platform {

Result<void> NativeSystemServices::openUrl(std::string_view url) {
    if (!UrlPolicy::allowed(url)) {
        return Result<void>::failure({"system_url_refused", "Only web addresses open in the default browser", std::string(url)});
    }

    return LinuxProcess::spawn({"xdg-open", std::string(url)}, true, "system_url_failed");
}

Result<void> NativeSystemServices::revealPath(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::is_directory(path, error) ? path : path.parent_path();
    return LinuxProcess::spawn({"xdg-open", folder.string()}, true, "system_reveal_failed");
}

// The locale variables are read in the order the C library itself resolves messages, so the product speaks the language the session does.
std::string NativeSystemServices::locale() {
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);

        if (value != nullptr && value[0] != '\0') {
            return value;
        }
    }

    return {};
}

// The home of the account answers when the environment names none, which a service started by the system may do.
std::filesystem::path NativeSystemServices::home() {
    if (const char* variable = std::getenv("HOME"); variable != nullptr && variable[0] == '/') {
        return variable;
    }

    passwd account{};
    passwd* found = nullptr;
    std::array<char, 4096> buffer{};

    if (::getpwuid_r(::getuid(), &account, buffer.data(), buffer.size(), &found) == 0 && found != nullptr && found->pw_dir != nullptr) {
        return found->pw_dir;
    }

    return "/";
}

// The downloads folder is the one the user directories of the desktop name, and a desktop that names none keeps downloads in the home folder under the name the specification suggests.
std::filesystem::path NativeSystemServices::downloads() {
    if (const gchar* folder = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD); folder != nullptr && folder[0] == '/') {
        return folder;
    }

    return home() / "Downloads";
}

// The zone the system is set to, named as the time zone database names it, such as `Europe/Lisbon`.
Result<std::string> NativeSystemServices::timeZone() {
    return TimeZoneDatabase::current();
}

std::int64_t NativeSystemServices::processId() {
    return static_cast<std::int64_t>(::getpid());
}

// Every family fontconfig knows as monospaced is offered with the file of its upright face closest to the regular weight.
std::vector<InstalledFont> NativeSystemServices::monospaceFonts() {
    FcConfig* config = FcInitLoadConfigAndFonts();
    FcPattern* pattern = FcPatternCreate();
    FcPatternAddInteger(pattern, FC_SPACING, FC_MONO);

    // Only outline faces without color reach the atlas, so a bitmap emoji face that declares itself monospaced is left out.
    FcPatternAddBool(pattern, FC_OUTLINE, FcTrue);
    FcPatternAddBool(pattern, FC_COLOR, FcFalse);
    FcObjectSet* objects = FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_WEIGHT, FC_SLANT, nullptr);
    FcFontSet* set = FcFontList(config, pattern, objects);
    std::map<std::string, std::pair<int, std::string>> chosen;

    for (int index = 0; set != nullptr && index < set->nfont; ++index) {
        FcChar8* family = nullptr;
        FcChar8* file = nullptr;
        int weight = FC_WEIGHT_REGULAR;
        int slant = FC_SLANT_ROMAN;

        if (FcPatternGetString(set->fonts[index], FC_FAMILY, 0, &family) != FcResultMatch || FcPatternGetString(set->fonts[index], FC_FILE, 0, &file) != FcResultMatch) {
            continue;
        }

        FcPatternGetInteger(set->fonts[index], FC_WEIGHT, 0, &weight);
        FcPatternGetInteger(set->fonts[index], FC_SLANT, 0, &slant);
        const int distance = std::abs(weight - FC_WEIGHT_REGULAR);
        const std::string name(reinterpret_cast<const char*>(family));
        const auto known = chosen.find(name);

        if (slant == FC_SLANT_ROMAN && (known == chosen.end() || distance < known->second.first)) {
            chosen[name] = {distance, std::string(reinterpret_cast<const char*>(file))};
        }
    }

    std::vector<InstalledFont> fonts;

    for (const auto& [family, entry] : chosen) {
        fonts.push_back({family, std::filesystem::path(entry.second)});
    }

    if (set != nullptr) {
        FcFontSetDestroy(set);
    }

    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
    FcConfigDestroy(config);

    return fonts;
}

Result<int> NativeSystemServices::zoneOffset(std::string_view zone, std::int64_t seconds) {
    return TimeZoneDatabase::offset(zone, seconds);
}

Result<void> NativeSystemServices::relaunch(const std::vector<std::string>& arguments) {
    std::array<char, 4096> buffer{};
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);

    if (length <= 0) {
        return Result<void>::failure({"system_relaunch_failed", "The product executable could not be located", {}});
    }

    std::vector<std::string> command{std::string(buffer.data(), static_cast<std::size_t>(length))};
    command.insert(command.end(), arguments.begin(), arguments.end());

    return LinuxProcess::spawn(command, false, "system_relaunch_failed");
}

} // namespace workpane::platform
