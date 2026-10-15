#pragma once

#include "Error.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace workpane::audio {

// The sounds the product plays for its plugins, each started from a file and known by an identity until it ends, is stopped or turns out unplayable.
class AudioOutput {
  public:
    using WakeHandler = std::function<void()>;

    struct Ended final {
        std::uint64_t sound;
        std::optional<Error> failure;
    };

    AudioOutput() = default;
    virtual ~AudioOutput() = default;

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    [[nodiscard]] virtual std::uint64_t play(const std::filesystem::path& file, float volume, bool loop) = 0;
    virtual void stop(std::uint64_t sound) = 0;
    [[nodiscard]] virtual std::vector<Ended> update() = 0;
    virtual void setWakeHandler(WakeHandler wake) = 0;
};

} // namespace workpane::audio
