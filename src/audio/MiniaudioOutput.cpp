#include "audio/MiniaudioOutput.h"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <optional>
#include <tuple>
#include <utility>

namespace workpane::audio {

// A silent output plays through the null backend of miniaudio, which decodes and advances every sound without a device, as the suite needs.
MiniaudioOutput::MiniaudioOutput(bool silent) : m_silent(silent) {}

MiniaudioOutput::~MiniaudioOutput() {
    {
        const std::lock_guard lock(m_mutex);
        m_stopping = true;
    }

    m_requested.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    for (const Opened& opened : m_opened) {
        close(*opened.playing);
    }

    for (const auto& [sound, playing] : m_sounds) {
        close(*playing);
    }

    m_opened.clear();
    m_sounds.clear();

    if (m_ready) {
        ma_engine_uninit(m_engine.get());
        ma_context_uninit(m_context.get());
    }
}

// The worker calls the handler under the same lock, so once a new handler is set the previous one is never called again.
void MiniaudioOutput::setWakeHandler(WakeHandler wake) {
    const std::lock_guard lock(m_mutex);
    m_wake = std::move(wake);
}

// A sound is opened by the worker, and one that cannot be heard, because the machine has no device or its file cannot be opened, ends at a later update with the reason.
std::uint64_t MiniaudioOutput::play(const std::filesystem::path& file, float volume, bool loop) {
    const std::uint64_t sound = ++m_next;

    {
        const std::lock_guard lock(m_mutex);
        m_waiting.push_back({sound, file, volume, loop});
    }

    m_requested.notify_one();

    // The worker starts with the first sound, because a system can take a long moment to open its device.
    if (!m_worker.joinable()) {
        // clang-format off
        m_worker = std::thread([this]() { run(); });
        // clang-format on
    }

    return sound;
}

// A sound still waiting or being opened never starts, and one already playing stops at once.
void MiniaudioOutput::stop(std::uint64_t sound) {
    {
        const std::lock_guard lock(m_mutex);
        // clang-format off
        std::erase_if(m_waiting, [sound](const Request& request) { return request.sound == sound; });
        std::erase_if(m_failed, [sound](const Ended& ended) { return ended.sound == sound; });
        // clang-format on

        if (m_opening == sound) {
            m_cancelled = sound;
        }

        if (const auto opened = std::ranges::find(m_opened, sound, &Opened::sound); opened != m_opened.end()) {
            close(*opened->playing);
            m_opened.erase(opened);
        }
    }

    release(sound);
}

std::vector<AudioOutput::Ended> MiniaudioOutput::update() {
    std::vector<Opened> opened;
    std::vector<Ended> ended;

    {
        const std::lock_guard lock(m_mutex);
        opened = std::exchange(m_opened, {});
        ended = std::exchange(m_failed, {});
    }

    for (Opened& sound : opened) {
        m_sounds.emplace(sound.sound, std::move(sound.playing));
    }

    for (const auto& [sound, playing] : m_sounds) {
        if (!ma_sound_is_looping(playing.get()) && ma_sound_at_end(playing.get())) {
            ended.push_back({sound, std::nullopt});
        }
    }

    for (const Ended& sound : ended) {
        release(sound.sound);
    }

    return ended;
}

Error MiniaudioOutput::unavailable(const std::filesystem::path& file) {
    return {"audio_unavailable", "The machine has no audio device the product could open", file.filename().string()};
}

void MiniaudioOutput::close(ma_sound& sound) {
    ma_sound_uninit(&sound);
}

void MiniaudioOutput::run() {
    const bool ready = openDevice();

    for (;;) {
        Request request{};

        {
            std::unique_lock lock(m_mutex);
            // clang-format off
            m_requested.wait(lock, [this]() { return m_stopping || !m_waiting.empty(); });
            // clang-format on

            if (m_stopping) {
                return;
            }

            request = std::move(m_waiting.front());
            m_waiting.erase(m_waiting.begin());
            m_opening = request.sound;
        }

        auto opened = ready ? open(request) : Result<std::unique_ptr<ma_sound>>::failure(unavailable(request.file));
        const std::lock_guard lock(m_mutex);
        m_opening = 0;

        if (m_cancelled == request.sound) {
            m_cancelled = 0;

            if (opened.hasValue()) {
                close(*opened.value());
            }

            continue;
        }

        if (opened.hasValue()) {
            m_opened.push_back({request.sound, std::move(opened.value())});
        } else {
            m_failed.push_back({request.sound, opened.error()});
        }

        if (m_wake) {
            m_wake();
        }
    }
}

// The null backend stands in for a device when the output is silent, and a machine that offers no device leaves every sound unheard.
bool MiniaudioOutput::openDevice() {
    m_context = std::make_unique<ma_context>();
    m_engine = std::make_unique<ma_engine>();
    const std::array<ma_backend, 1> silent{ma_backend_null};
    const bool opened = ma_context_init(m_silent ? silent.data() : nullptr, m_silent ? 1U : 0U, nullptr, m_context.get()) == MA_SUCCESS;
    ma_engine_config config = ma_engine_config_init();
    config.pContext = m_context.get();
    const bool started = opened && ma_engine_init(&config, m_engine.get()) == MA_SUCCESS;

    if (opened && !started) {
        ma_context_uninit(m_context.get());
    }

    m_ready = started;

    return started;
}

// A sound decodes on the job threads of miniaudio and starts once it is decoded.
Result<std::unique_ptr<ma_sound>> MiniaudioOutput::open(const Request& request) {
    auto sound = std::make_unique<ma_sound>();
    const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC;
#if defined(_WIN32)
    const ma_result opened = ma_sound_init_from_file_w(m_engine.get(), request.file.wstring().c_str(), flags, nullptr, nullptr, sound.get());
#else
    const ma_result opened = ma_sound_init_from_file(m_engine.get(), request.file.c_str(), flags, nullptr, nullptr, sound.get());
#endif

    if (opened != MA_SUCCESS) {
        return Result<std::unique_ptr<ma_sound>>::failure({"audio_file_unreadable", "A sound file could not be opened", request.file.filename().string()});
    }

    ma_sound_set_volume(sound.get(), request.volume);
    ma_sound_set_looping(sound.get(), request.loop ? MA_TRUE : MA_FALSE);
    std::ignore = ma_sound_start(sound.get());

    return Result<std::unique_ptr<ma_sound>>::success(std::move(sound));
}

void MiniaudioOutput::release(std::uint64_t sound) {
    const auto found = m_sounds.find(sound);

    if (found == m_sounds.end()) {
        return;
    }

    close(*found->second);
    m_sounds.erase(found);
}

} // namespace workpane::audio
