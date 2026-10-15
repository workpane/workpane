#include "support/FakeProcessLauncher.h"

#include "support/FakeProcess.h"

#include <string>
#include <utility>

namespace workpane::tests {

FakeProcessLauncher::FakeProcessLauncher(std::shared_ptr<ProcessRecord> record) : m_record(std::move(record)) {}

Result<std::unique_ptr<process::Process>> FakeProcessLauncher::start(const process::ProcessLaunch& launch, process::ProcessEvents events) {
    if (m_record->refusal.has_value()) {
        return Result<std::unique_ptr<process::Process>>::failure(*m_record->refusal);
    }

    m_record->launches.push_back(launch);
    m_record->events.push_back(std::move(events));
    m_record->inputs.emplace_back();
    m_record->stopped.push_back(false);
    m_record->ended.push_back(false);

    return Result<std::unique_ptr<process::Process>>::success(std::make_unique<FakeProcess>(m_record, m_record->launches.size() - 1));
}

std::optional<std::filesystem::path> FakeProcessLauncher::find(std::string_view name, const std::vector<std::filesystem::path>&) const {
    const auto found = m_record->executables.find(std::string(name));
    return found == m_record->executables.end() ? std::nullopt : std::optional<std::filesystem::path>(found->second);
}

} // namespace workpane::tests
