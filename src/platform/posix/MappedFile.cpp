#include "platform/MappedFile.h"

#include "platform/PathTextHelper.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace workpane::platform {

Result<MappedFile> MappedFile::open(const std::filesystem::path& file) {
    const int descriptor = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);

    if (descriptor < 0) {
        return Result<MappedFile>::failure({"file_map_failed", "The file could not be opened to be mapped", PathTextHelper::utf8(file)});
    }

    struct stat status{};
    const bool measured = ::fstat(descriptor, &status) == 0 && status.st_size > 0;
    void* view = measured ? ::mmap(nullptr, static_cast<std::size_t>(status.st_size), PROT_READ | PROT_WRITE, MAP_PRIVATE, descriptor, 0) : MAP_FAILED;
    ::close(descriptor);

    if (view == MAP_FAILED) {
        return Result<MappedFile>::failure({"file_map_failed", "The file could not be mapped", PathTextHelper::utf8(file)});
    }

    return Result<MappedFile>::success(MappedFile(view, static_cast<std::size_t>(status.st_size)));
}

void MappedFile::release() {
    if (m_view != nullptr) {
        ::munmap(m_view, m_size);
    }
}

} // namespace workpane::platform
