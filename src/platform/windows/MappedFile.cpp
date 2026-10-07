#include "platform/MappedFile.h"

#include "platform/PathTextHelper.h"

#include <windows.h>

namespace workpane::platform {

Result<MappedFile> MappedFile::open(const std::filesystem::path& file) {
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        return Result<MappedFile>::failure({"file_map_failed", "The file could not be opened to be mapped", PathTextHelper::utf8(file)});
    }

    LARGE_INTEGER length{};
    const bool measured = GetFileSizeEx(handle, &length) != FALSE && length.QuadPart > 0;
    HANDLE mapping = measured ? CreateFileMappingW(handle, nullptr, PAGE_WRITECOPY, 0, 0, nullptr) : nullptr;
    void* view = mapping != nullptr ? MapViewOfFile(mapping, FILE_MAP_COPY, 0, 0, 0) : nullptr;

    if (mapping != nullptr) {
        CloseHandle(mapping);
    }

    CloseHandle(handle);

    if (view == nullptr) {
        return Result<MappedFile>::failure({"file_map_failed", "The file could not be mapped", PathTextHelper::utf8(file)});
    }

    return Result<MappedFile>::success(MappedFile(view, static_cast<std::size_t>(length.QuadPart)));
}

void MappedFile::release() {
    if (m_view != nullptr) {
        UnmapViewOfFile(m_view);
    }
}

} // namespace workpane::platform
