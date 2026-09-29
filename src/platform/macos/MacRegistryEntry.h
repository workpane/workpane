#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace workpane::platform {

// One object of the IOKit registry, released when it goes out of scope, whose properties read as text, numbers and flags.
class MacRegistryEntry final {
  public:
    explicit MacRegistryEntry(io_registry_entry_t entry);
    ~MacRegistryEntry();

    MacRegistryEntry(MacRegistryEntry&& other) noexcept;
    MacRegistryEntry& operator=(MacRegistryEntry&& other) noexcept;
    MacRegistryEntry(const MacRegistryEntry&) = delete;
    MacRegistryEntry& operator=(const MacRegistryEntry&) = delete;

    [[nodiscard]] static std::vector<MacRegistryEntry> matching(const char* className);
    [[nodiscard]] static std::optional<MacRegistryEntry> first(const char* className);
    [[nodiscard]] static std::optional<MacRegistryEntry> forBsdName(const std::string& name);

    [[nodiscard]] std::string name() const;
    [[nodiscard]] bool conformsTo(const char* className) const;
    [[nodiscard]] std::string text(const char* key) const;
    [[nodiscard]] std::int64_t number(const char* key) const;
    [[nodiscard]] bool flag(const char* key) const;
    [[nodiscard]] std::string nestedText(const char* dictionary, const char* key) const;
    [[nodiscard]] std::optional<MacRegistryEntry> parent() const;
    [[nodiscard]] std::uint64_t identity() const;
    [[nodiscard]] std::string hardwareAddress(const char* key) const;

  private:
    [[nodiscard]] static std::string string(CFTypeRef value);
    [[nodiscard]] static std::int64_t integer(CFTypeRef value);

    [[nodiscard]] CFTypeRef copyProperty(const char* key) const;

    io_registry_entry_t m_entry;
};

} // namespace workpane::platform
