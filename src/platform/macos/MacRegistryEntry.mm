#include "platform/macos/MacRegistryEntry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

namespace workpane::platform {

MacRegistryEntry::MacRegistryEntry(io_registry_entry_t entry) : m_entry(entry) {}

MacRegistryEntry::~MacRegistryEntry() {
    if (m_entry != IO_OBJECT_NULL) {
        IOObjectRelease(m_entry);
    }
}

MacRegistryEntry::MacRegistryEntry(MacRegistryEntry&& other) noexcept : m_entry(std::exchange(other.m_entry, IO_OBJECT_NULL)) {}

MacRegistryEntry& MacRegistryEntry::operator=(MacRegistryEntry&& other) noexcept {
    if (this != &other && m_entry != IO_OBJECT_NULL) {
        IOObjectRelease(m_entry);
    }

    m_entry = std::exchange(other.m_entry, IO_OBJECT_NULL);

    return *this;
}

std::vector<MacRegistryEntry> MacRegistryEntry::matching(const char* className) {
    std::vector<MacRegistryEntry> entries;
    io_iterator_t iterator = IO_OBJECT_NULL;

    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(className), &iterator) != KERN_SUCCESS) {
        return entries;
    }

    for (io_object_t entry = IOIteratorNext(iterator); entry != IO_OBJECT_NULL; entry = IOIteratorNext(iterator)) {
        entries.emplace_back(entry);
    }

    IOObjectRelease(iterator);

    return entries;
}

std::optional<MacRegistryEntry> MacRegistryEntry::first(const char* className) {
    const io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching(className));

    if (service == IO_OBJECT_NULL) {
        return std::nullopt;
    }

    return MacRegistryEntry(service);
}

std::optional<MacRegistryEntry> MacRegistryEntry::forBsdName(const std::string& name) {
    const io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOBSDNameMatching(kIOMainPortDefault, 0, name.c_str()));

    if (service == IO_OBJECT_NULL) {
        return std::nullopt;
    }

    return MacRegistryEntry(service);
}

std::string MacRegistryEntry::name() const {
    io_name_t buffer{};

    if (IORegistryEntryGetName(m_entry, buffer) != KERN_SUCCESS) {
        return {};
    }

    return buffer;
}

bool MacRegistryEntry::conformsTo(const char* className) const {
    return IOObjectConformsTo(m_entry, className) != 0;
}

std::string MacRegistryEntry::text(const char* key) const {
    const CFTypeRef value = copyProperty(key);

    if (value == nullptr) {
        return {};
    }

    std::string written = string(value);
    CFRelease(value);

    return written;
}

std::int64_t MacRegistryEntry::number(const char* key) const {
    const CFTypeRef value = copyProperty(key);

    if (value == nullptr) {
        return 0;
    }

    const std::int64_t read = integer(value);
    CFRelease(value);

    return read;
}

bool MacRegistryEntry::flag(const char* key) const {
    const CFTypeRef value = copyProperty(key);

    if (value == nullptr) {
        return false;
    }

    const bool set = CFGetTypeID(value) == CFBooleanGetTypeID() && CFBooleanGetValue(static_cast<CFBooleanRef>(value));
    CFRelease(value);

    return set;
}

std::string MacRegistryEntry::nestedText(const char* dictionary, const char* key) const {
    const CFTypeRef value = copyProperty(dictionary);

    if (value == nullptr) {
        return {};
    }

    std::string written;

    if (CFGetTypeID(value) == CFDictionaryGetTypeID()) {
        const CFStringRef inner = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
        written = string(CFDictionaryGetValue(static_cast<CFDictionaryRef>(value), inner));
        CFRelease(inner);
    }

    CFRelease(value);

    return written;
}

std::optional<MacRegistryEntry> MacRegistryEntry::parent() const {
    io_registry_entry_t parent = IO_OBJECT_NULL;

    if (IORegistryEntryGetParentEntry(m_entry, kIOServicePlane, &parent) != KERN_SUCCESS) {
        return std::nullopt;
    }

    return MacRegistryEntry(parent);
}

std::uint64_t MacRegistryEntry::identity() const {
    std::uint64_t identifier = 0;
    IORegistryEntryGetRegistryEntryID(m_entry, &identifier);

    return identifier;
}

std::string MacRegistryEntry::hardwareAddress(const char* key) const {
    const CFTypeRef value = copyProperty(key);

    if (value == nullptr) {
        return {};
    }

    std::string written;

    if (CFGetTypeID(value) == CFDataGetTypeID()) {
        const auto data = static_cast<CFDataRef>(value);

        for (CFIndex index = 0; index < CFDataGetLength(data); ++index) {
            std::array<char, 4> part{};
            std::snprintf(part.data(), part.size(), index == 0 ? "%02x" : ":%02x", CFDataGetBytePtr(data)[index]);
            written += part.data();
        }
    }

    CFRelease(value);

    return written;
}

// A property arrives as a string or as raw bytes spelling text, which drivers use for model and vendor names.
std::string MacRegistryEntry::string(CFTypeRef value) {
    if (value == nullptr) {
        return {};
    }

    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        const auto text = static_cast<CFStringRef>(value);
        const CFIndex capacity = CFStringGetMaximumSizeForEncoding(CFStringGetLength(text), kCFStringEncodingUTF8) + 1;
        std::string buffer(static_cast<std::size_t>(capacity), '\0');

        if (!CFStringGetCString(text, buffer.data(), capacity, kCFStringEncodingUTF8)) {
            return {};
        }

        buffer.resize(std::strlen(buffer.c_str()));
        return buffer;
    }

    if (CFGetTypeID(value) == CFDataGetTypeID()) {
        const auto data = static_cast<CFDataRef>(value);
        const auto* bytes = reinterpret_cast<const char*>(CFDataGetBytePtr(data));
        return std::string(bytes, strnlen(bytes, static_cast<std::size_t>(CFDataGetLength(data))));
    }

    return {};
}

// A number arrives as a number or as raw little endian bytes, which PCI drivers use for their identifiers.
std::int64_t MacRegistryEntry::integer(CFTypeRef value) {
    if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        std::int64_t read = 0;
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &read);
        return read;
    }

    if (CFGetTypeID(value) == CFDataGetTypeID()) {
        const auto data = static_cast<CFDataRef>(value);
        const CFIndex length = std::min<CFIndex>(CFDataGetLength(data), 8);
        std::int64_t read = 0;

        for (CFIndex index = length - 1; index >= 0; --index) {
            read = (read << 8) | CFDataGetBytePtr(data)[index];
        }

        return read;
    }

    return 0;
}

CFTypeRef MacRegistryEntry::copyProperty(const char* key) const {
    const CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    const CFTypeRef value = IORegistryEntryCreateCFProperty(m_entry, name, kCFAllocatorDefault, 0);
    CFRelease(name);

    return value;
}

} // namespace workpane::platform
