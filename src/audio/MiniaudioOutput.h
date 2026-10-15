#pragma once

#include "Error.h"
#include "Result.h"
#include "audio/AudioOutput.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct ma_context;
struct ma_engine;
struct ma_sound;

namespace workpane::audio {

// Plays sounds through miniaudio from a thread of its own, which opens the device the first time a sound is asked for and opens every sound file, so the interface thread never waits for audio or for the disk.
class MiniaudioOutput final : public AudioOutput {
  public:
    explicit MiniaudioOutput(bool silent);
    ~MiniaudioOutput() override;

    [[nodiscard]] std::uint64_t play(const std::filesystem::path& file, float volume, bool loop) override;
    void stop(std::uint64_t sound) override;
    [[nodiscard]] std::vector<Ended> update() override;
    void setWakeHandler(WakeHandler wake) override;

  private:
    struct Request final {
        std::uint64_t sound;
        std::filesystem::path file;
        float volume;
        bool loop;
    };

    struct Opened final {
        std::uint64_t sound;
        std::unique_ptr<ma_sound> playing;
    };

    [[nodiscard]] static Error unavailable(const std::filesystem::path& file);
    static void close(ma_sound& sound);

    void run();
    [[nodiscard]] bool openDevice();
    [[nodiscard]] Result<std::unique_ptr<ma_sound>> open(const Request& request);
    void release(std::uint64_t sound);

    bool m_silent;
    bool m_ready{false};
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_requested;
    bool m_stopping{false};
    std::uint64_t m_opening{0};
    std::uint64_t m_cancelled{0};
    WakeHandler m_wake;
    std::unique_ptr<ma_context> m_context;
    std::unique_ptr<ma_engine> m_engine;
    std::vector<Request> m_waiting;
    std::vector<Opened> m_opened;
    std::vector<Ended> m_failed;
    std::map<std::uint64_t, std::unique_ptr<ma_sound>> m_sounds;
    std::uint64_t m_next{0};
};

} // namespace workpane::audio
