#include "support/FakePseudoTerminal.h"

#include <mutex>
#include <utility>

namespace workpane::tests {

FakePseudoTerminal::FakePseudoTerminal(std::shared_ptr<TerminalProcessRecord> process) : m_process(std::move(process)) {}

FakePseudoTerminal::~FakePseudoTerminal() {
    const std::lock_guard lock(m_process->mutex);
    m_process->alive = false;
}

std::string FakePseudoTerminal::takeOutput(std::size_t limit) {
    const std::lock_guard lock(m_process->mutex);
    std::string taken = m_process->output.substr(0, limit);
    m_process->output.erase(0, taken.size());

    return taken;
}

std::optional<int> FakePseudoTerminal::exitCode() const {
    const std::lock_guard lock(m_process->mutex);
    return m_process->output.empty() ? m_process->exit : std::nullopt;
}

bool FakePseudoTerminal::write(std::string_view bytes) {
    const std::lock_guard lock(m_process->mutex);

    if (m_process->exit.has_value() || m_process->refusesInput) {
        return false;
    }

    m_process->input.append(bytes);

    return true;
}

void FakePseudoTerminal::resize(int columns, int rows) {
    const std::lock_guard lock(m_process->mutex);
    m_process->sizes.emplace_back(columns, rows);
}

std::string FakePseudoTerminal::directory() const {
    const std::lock_guard lock(m_process->mutex);
    return m_process->directory;
}

} // namespace workpane::tests
