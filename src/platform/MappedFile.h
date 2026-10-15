#pragma once

#include "Result.h"

#include <cstddef>
#include <filesystem>

namespace workpane::platform {

// A file read through the memory of the system, so only the parts somebody reads are loaded, privately, so a write never reaches the file.
class MappedFile final {
  public:
    [[nodiscard]] static Result<MappedFile> open(const std::filesystem::path& file);

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    ~MappedFile();

    [[nodiscard]] unsigned char* data() const;
    [[nodiscard]] std::size_t size() const;

  private:
    MappedFile(void* view, std::size_t size);

    void release();

    void* m_view{nullptr};
    std::size_t m_size{0};
};

} // namespace workpane::platform
