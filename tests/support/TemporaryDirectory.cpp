#include "support/TemporaryDirectory.h"

#include <cstdint>
#include <random>
#include <string>
#include <system_error>

namespace workpane::tests {

// The name is drawn at random until a folder of that name is created anew, so tests running at once in other processes never share one.
TemporaryDirectory::TemporaryDirectory() {
    std::random_device device;
    std::uniform_int_distribution<std::uint64_t> draw;

    do {
        m_path = std::filesystem::temp_directory_path() / ("workpane-test-" + std::to_string(draw(device)));
    } while (!std::filesystem::create_directory(m_path));
}

TemporaryDirectory::~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(m_path, error);
}

const std::filesystem::path& TemporaryDirectory::path() const {
    return m_path;
}

} // namespace workpane::tests
