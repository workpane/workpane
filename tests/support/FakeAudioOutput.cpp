#include "support/FakeAudioOutput.h"

#include <utility>

namespace workpane::tests {

FakeAudioOutput::FakeAudioOutput(std::shared_ptr<AudioRecord> record) : m_record(std::move(record)) {}

std::uint64_t FakeAudioOutput::play(const std::filesystem::path& file, float volume, bool loop) {
    m_record->played.push_back({++m_next, file, volume, loop});

    if (const auto failure = m_record->failures.find(file.filename().string()); failure != m_record->failures.end()) {
        m_ended.push_back({m_next, failure->second});
    }

    return m_next;
}

void FakeAudioOutput::stop(std::uint64_t sound) {
    m_record->stopped.push_back(sound);
}

std::vector<audio::AudioOutput::Ended> FakeAudioOutput::update() {
    return std::exchange(m_ended, {});
}

void FakeAudioOutput::setWakeHandler(WakeHandler) {}

} // namespace workpane::tests
