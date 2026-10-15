#include "support/FakeSystemServices.h"

#include "platform/UrlPolicy.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace workpane::tests {

FakeSystemServices::FakeSystemServices(std::shared_ptr<SystemRecord> record) : m_record(std::move(record)) {}

Result<std::optional<std::string>> FakeSystemServices::readCredential(std::string_view service, std::string_view account) {
    const std::lock_guard lock(m_record->credentialMutex);

    if (m_record->credentialsFail) {
        return Result<std::optional<std::string>>::failure({"credential_store_unavailable", "The system credential store is unavailable", ""});
    }

    const auto found = m_record->credentials.find({std::string(service), std::string(account)});

    return Result<std::optional<std::string>>::success(found == m_record->credentials.end() ? std::nullopt : std::optional<std::string>(found->second));
}

Result<void> FakeSystemServices::writeCredential(std::string_view service, std::string_view account, std::string_view value) {
    const std::lock_guard lock(m_record->credentialMutex);

    if (m_record->credentialsFail) {
        return Result<void>::failure({"credential_store_unavailable", "The system credential store is unavailable", ""});
    }

    m_record->credentials[{std::string(service), std::string(account)}] = std::string(value);

    return Result<void>::success();
}

Result<void> FakeSystemServices::eraseCredential(std::string_view service, std::string_view account) {
    const std::lock_guard lock(m_record->credentialMutex);

    if (m_record->credentialsFail) {
        return Result<void>::failure({"credential_store_unavailable", "The system credential store is unavailable", ""});
    }

    m_record->credentials.erase({std::string(service), std::string(account)});

    return Result<void>::success();
}

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

std::vector<platform::FallbackFace> FakeSystemServices::fallbackFaces() {
    return m_record->fallbackFaces;
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
