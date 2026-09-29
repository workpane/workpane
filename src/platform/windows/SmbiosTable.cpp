#include "platform/windows/SmbiosTable.h"

#include <windows.h>

#include <algorithm>

namespace workpane::platform {

// The raw firmware table starts with an eight byte header, and every structure after it is a formatted area followed by its strings and a double terminator.
SmbiosTable::SmbiosTable() {
    const UINT size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);

    if (size <= headerSize) {
        return;
    }

    m_table.resize(size);

    if (GetSystemFirmwareTable('RSMB', 0, m_table.data(), size) != size) {
        m_table.clear();
    }
}

// A module of size zero is an empty slot, a size with its high bit set counts kilobytes, and the largest modules move their size to the extended field.
nlohmann::json SmbiosTable::memoryModules() const {
    nlohmann::json modules = nlohmann::json::array();

    for (const std::size_t structure : structures(17)) {
        const std::uint32_t size = integer(structure, 0x0C, 2);

        if (size == 0 || size == 0xFFFF) {
            continue;
        }

        const std::uint64_t megabytes = size == 0x7FFF ? integer(structure, 0x1C, 4) : (size & 0x8000) != 0 ? (size & 0x7FFF) / 1024 : size;
        const std::uint32_t configured = length(structure) > 0x21 ? integer(structure, 0x20, 2) : 0;
        const std::uint32_t speed = configured != 0 ? configured : integer(structure, 0x15, 2);
        const std::string vendor = string(structure, 0x17);
        const std::string model = string(structure, 0x1A);
        modules.push_back({{"vendor", vendor}, {"name", string(structure, 0x10)}, {"model", model}, {"serial", string(structure, 0x18)}, {"size", megabytes * 1024 * 1024}, {"frequency", static_cast<std::uint64_t>(speed) * 1000000}});
    }

    return modules;
}

nlohmann::json SmbiosTable::baseboard() const {
    const auto boards = structures(2);

    if (boards.empty()) {
        return {{"vendor", ""}, {"name", ""}, {"version", ""}, {"serial", ""}};
    }

    return {{"vendor", string(boards.front(), 0x04)}, {"name", string(boards.front(), 0x05)}, {"version", string(boards.front(), 0x06)}, {"serial", string(boards.front(), 0x07)}};
}

std::vector<std::size_t> SmbiosTable::structures(std::uint8_t type) const {
    std::vector<std::size_t> found;
    std::size_t position = headerSize;

    while (position + 4 <= m_table.size()) {
        const std::size_t formatted = m_table[position + 1];

        if (formatted < 4 || position + formatted > m_table.size()) {
            break;
        }

        if (m_table[position] == type) {
            found.push_back(position);
        }

        std::size_t next = position + formatted;

        while (next + 1 < m_table.size() && !(m_table[next] == 0 && m_table[next + 1] == 0)) {
            ++next;
        }

        position = next + 2;
    }

    return found;
}

// A string field holds the one based number of a string in the set after the formatted area, and zero means the firmware left it out.
std::string SmbiosTable::string(std::size_t structure, std::size_t offset) const {
    const std::size_t formatted = length(structure);

    if (offset >= formatted) {
        return {};
    }

    const std::uint8_t number = m_table[structure + offset];
    std::size_t position = structure + formatted;

    for (std::uint8_t index = 1; number != 0 && position < m_table.size() && m_table[position] != 0; ++index) {
        const auto end = std::find(m_table.begin() + static_cast<std::ptrdiff_t>(position), m_table.end(), std::uint8_t{0});
        std::string text(m_table.begin() + static_cast<std::ptrdiff_t>(position), end);

        if (index == number) {
            text.erase(text.find_last_not_of(' ') + 1);
            return text;
        }

        position = static_cast<std::size_t>(end - m_table.begin()) + 1;
    }

    return {};
}

std::uint32_t SmbiosTable::integer(std::size_t structure, std::size_t offset, std::size_t width) const {
    if (offset + width > length(structure)) {
        return 0;
    }

    std::uint32_t value = 0;

    for (std::size_t index = width; index > 0; --index) {
        value = (value << 8) | m_table[structure + offset + index - 1];
    }

    return value;
}

std::size_t SmbiosTable::length(std::size_t structure) const {
    return m_table[structure + 1];
}

} // namespace workpane::platform
