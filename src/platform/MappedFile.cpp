#include "platform/MappedFile.h"

#include <utility>

namespace workpane::platform {

MappedFile::MappedFile(void* view, std::size_t size) : m_view(view), m_size(size) {}

MappedFile::MappedFile(MappedFile&& other) noexcept : m_view(std::exchange(other.m_view, nullptr)), m_size(std::exchange(other.m_size, 0)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    release();
    m_view = std::exchange(other.m_view, nullptr);
    m_size = std::exchange(other.m_size, 0);

    return *this;
}

MappedFile::~MappedFile() {
    release();
}

unsigned char* MappedFile::data() const {
    return static_cast<unsigned char*>(m_view);
}

std::size_t MappedFile::size() const {
    return m_size;
}

} // namespace workpane::platform
