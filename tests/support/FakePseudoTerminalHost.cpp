#include "support/FakePseudoTerminalHost.h"

#include "support/FakePseudoTerminal.h"
#include "support/RootedPath.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace workpane::tests {

FakePseudoTerminalHost::FakePseudoTerminalHost(std::shared_ptr<TerminalRecord> record) : m_record(std::move(record)) {}

void FakePseudoTerminalHost::listen(std::function<void()>) {}

Result<std::unique_ptr<ui::PseudoTerminal>> FakePseudoTerminalHost::start(const ui::TerminalLaunch& launch) {
    if (!m_record->refusal.empty()) {
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({m_record->refusal, "The recorded shell refused to start", launch.directory.string()});
    }

    // A directory that is gone is refused the way the platform refuses it, before any shell starts.
    if (std::error_code error; !std::filesystem::is_directory(launch.directory, error)) {
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_directory_missing", "The directory of the terminal does not exist", launch.directory.string()});
    }

    auto process = std::make_shared<TerminalProcessRecord>();
    process->launch = launch;
    process->directory = launch.directory.string();
    m_record->processes.push_back(process);

    return Result<std::unique_ptr<ui::PseudoTerminal>>::success(std::make_unique<FakePseudoTerminal>(process));
}

std::filesystem::path FakePseudoTerminalHost::shellProgram() const {
    return RootedPath::of("bin/sh");
}

std::string FakePseudoTerminalHost::quotePaths(const std::vector<std::filesystem::path>& paths, const std::filesystem::path&) const {
    std::string quoted;

    for (const auto& path : paths) {
        quoted += "'" + path.string() + "' ";
    }

    return quoted;
}

} // namespace workpane::tests
