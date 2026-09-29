#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace workpane::platform {

// The firmware description of the machine, from which the installed memory modules and the mainboard are read.
class SmbiosTable final {
  public:
    SmbiosTable();

    [[nodiscard]] nlohmann::json memoryModules() const;
    [[nodiscard]] nlohmann::json baseboard() const;

  private:
    static constexpr std::size_t headerSize{8};

    [[nodiscard]] std::vector<std::size_t> structures(std::uint8_t type) const;
    [[nodiscard]] std::string string(std::size_t structure, std::size_t offset) const;
    [[nodiscard]] std::uint32_t integer(std::size_t structure, std::size_t offset, std::size_t width) const;
    [[nodiscard]] std::size_t length(std::size_t structure) const;

    std::vector<std::uint8_t> m_table;
};

} // namespace workpane::platform
