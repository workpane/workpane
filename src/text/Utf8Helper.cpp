#include "text/Utf8Helper.h"

#include <algorithm>

namespace workpane::text {

std::size_t Utf8Helper::sequenceLength(unsigned char lead) {
    if (lead < 0x80U) {
        return 1;
    }

    if (lead >= 0xC2U && lead <= 0xDFU) {
        return 2;
    }

    if (lead >= 0xE0U && lead <= 0xEFU) {
        return 3;
    }

    return lead >= 0xF0U && lead <= 0xF4U ? 4 : 0;
}

// The second byte of a sequence is narrowed for the leads that could otherwise spell an overlong form, a surrogate or a value beyond Unicode.
bool Utf8Helper::follows(unsigned char lead, std::size_t offset, unsigned char byte) {
    if (offset > 1 || (lead != 0xE0U && lead != 0xEDU && lead != 0xF0U && lead != 0xF4U)) {
        return byte >= 0x80U && byte <= 0xBFU;
    }

    if (lead == 0xE0U) {
        return byte >= 0xA0U && byte <= 0xBFU;
    }

    if (lead == 0xEDU) {
        return byte >= 0x80U && byte <= 0x9FU;
    }

    return lead == 0xF0U ? byte >= 0x90U && byte <= 0xBFU : byte >= 0x80U && byte <= 0x8FU;
}

bool Utf8Helper::valid(std::string_view text) {
    std::size_t index = 0;

    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        const std::size_t length = sequenceLength(lead);

        if (length == 0 || index + length > text.size()) {
            return false;
        }

        for (std::size_t offset = 1; offset < length; ++offset) {
            if (!follows(lead, offset, static_cast<unsigned char>(text[index + offset]))) {
                return false;
            }
        }

        index += length;
    }

    return true;
}

std::string Utf8Helper::truncated(std::string_view text, std::size_t characters) {
    std::size_t index = 0;

    for (std::size_t counted = 0; counted < characters && index < text.size(); ++counted) {
        const std::size_t length = sequenceLength(static_cast<unsigned char>(text[index]));
        index += length == 0 ? 1 : length;
    }

    return std::string(text.substr(0, std::min(index, text.size())));
}

} // namespace workpane::text
