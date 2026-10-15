#pragma once

#include <filesystem>

namespace workpane::tests {

// Writes a short tone as a WAV file of sixteen bit mono samples, which the audio tests play through the product.
class WaveFile final {
  public:
    static void write(const std::filesystem::path& file, double seconds);

  private:
    static constexpr int sampleRate{8000};
};

} // namespace workpane::tests
