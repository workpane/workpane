#include "support/FakeSystemServices.h"

#include "platform/UrlPolicy.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace workpane::tests {

FakeSystemServices::FakeSystemServices(std::shared_ptr<SystemRecord> record) : m_record(std::move(record)) {}

Result<void> FakeSystemServices::openUrl(std::string_view url) {
    if (!platform::UrlPolicy::allowed(url)) {
        return Result<void>::failure({"system_url_refused", "Only web addresses open in the default browser", std::string(url)});
    }

    m_record->openedUrls.emplace_back(url);

    return Result<void>::success();
}

Result<void> FakeSystemServices::revealPath(const std::filesystem::path& path) {
    m_record->revealedPaths.push_back(path);

    return Result<void>::success();
}

std::string FakeSystemServices::locale() {
    return m_record->locale;
}

std::filesystem::path FakeSystemServices::home() {
    return std::filesystem::temp_directory_path();
}

std::filesystem::path FakeSystemServices::downloads() {
    return home() / "Downloads";
}

Result<std::string> FakeSystemServices::timeZone() {
    return Result<std::string>::success(m_record->timeZone);
}

std::int64_t FakeSystemServices::processId() {
    return m_record->processId;
}

std::vector<platform::InstalledFont> FakeSystemServices::monospaceFonts() {
    return m_record->fonts;
}

// Each zone the test names keeps one offset at every instant, so an occurrence is known before the test runs.
Result<int> FakeSystemServices::zoneOffset(std::string_view zone, std::int64_t) {
    const auto found = m_record->zoneOffsets.find(zone);

    if (found == m_record->zoneOffsets.end()) {
        return Result<int>::failure({"time_zone_unknown", "The time zone is not known to the system", std::string(zone)});
    }

    return Result<int>::success(found->second);
}

Result<void> FakeSystemServices::relaunch(const std::vector<std::string>& arguments) {
    m_record->relaunches.push_back(arguments);

    return Result<void>::success();
}

} // namespace workpane::tests
