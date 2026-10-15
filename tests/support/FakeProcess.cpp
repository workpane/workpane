#include "support/FakeProcess.h"

#include <utility>

namespace workpane::tests {

FakeProcess::FakeProcess(std::shared_ptr<ProcessRecord> record, std::size_t index) : m_record(std::move(record)), m_index(index) {}

bool FakeProcess::write(std::string_view bytes) {
    if (m_record->ended[m_index] || m_inputEnded) {
        return false;
    }

    m_record->inputs[m_index] += bytes;

    if (m_record->responder != nullptr) {
        m_record->responder(m_index, bytes);
    }

    return true;
}

bool FakeProcess::endInput() {
    if (m_record->ended[m_index]) {
        return false;
    }

    m_inputEnded = true;

    return true;
}

void FakeProcess::stop() {
    m_record->stopped[m_index] = true;

    if (m_record->endsOnStop && !m_record->ended[m_index]) {
        m_record->ended[m_index] = true;
        m_record->events[m_index].exited({stoppedCode, false});
    }
}

} // namespace workpane::tests
