#pragma once

#include "audio/AudioOutput.h"
#include "support/AudioRecord.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace workpane::tests {

// Records the sounds the product plays instead of opening a device.
class FakeAudioOutput final : public audio::AudioOutput {
  public:
    explicit FakeAudioOutput(std::shared_ptr<AudioRecord> record);

    [[nodiscard]] std::uint64_t play(const std::filesystem::path& file, float volume, bool loop) override;
    void stop(std::uint64_t sound) override;
    [[nodiscard]] std::vector<Ended> update() override;
    void setWakeHandler(WakeHandler wake) override;

  private:
    std::shared_ptr<AudioRecord> m_record;
    std::vector<Ended> m_ended;
    std::uint64_t m_next{0};
};

} // namespace workpane::tests
