#include "support/WaveFile.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace workpane::tests {

void WaveFile::write(const std::filesystem::path& file, double seconds) {
    const auto samples = static_cast<std::uint32_t>(seconds * sampleRate);
    std::vector<std::int16_t> tone(samples);

    for (std::uint32_t index = 0; index < samples; ++index) {
        tone[index] = static_cast<std::int16_t>(8000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(index) / sampleRate));
    }

    const std::uint32_t data = samples * 2;
    std::ofstream out(file, std::ios::binary);
    // clang-format off
    const auto put = [&out](std::uint32_t value, int bytes) { out.write(reinterpret_cast<const char*>(&value), bytes); };
    // clang-format on
    out.write("RIFF", 4);
    put(36 + data, 4);
    out.write("WAVEfmt ", 8);
    put(16, 4);
    put(1, 2);
    put(1, 2);
    put(sampleRate, 4);
    put(sampleRate * 2, 4);
    put(2, 2);
    put(16, 2);
    out.write("data", 4);
    put(data, 4);
    out.write(reinterpret_cast<const char*>(tone.data()), static_cast<std::streamsize>(data));
}

} // namespace workpane::tests
